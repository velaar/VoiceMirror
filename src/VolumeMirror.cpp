#define NOMINMAX  // Prevent windows.h from defining min and max macros

#include "VolumeMirror.h"

#include <chrono>
#include <mutex>
#include <thread>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"

using namespace VolumeUtils;

VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal,
                           float maxDbmVal, VoicemeeterManager& manager,
                           WindowsManager& windowsManager,
                           bool playSound)
    : channelIndex(channelIdx),
      channelType(type),
      minDbm(minDbmVal),
      maxDbm(maxDbmVal),
      vmManager(manager),
      windowsManager(windowsManager),
      lastWindowsState{0.0f, false},
      lastVmState{0.0f, false},
      running(false),
      pollingEnabled(false),
      pollingInterval(DEFAULT_POLLING_INTERVAL_MS),
      debounceDuration(DEBOUNCE_DURATION_MS),
      suppressionDuration(SUPPRESSION_DURATION_MS),
      suppressVoicemeeterUntil(std::chrono::steady_clock::time_point::min()),
      suppressWindowsUntil(std::chrono::steady_clock::time_point::min()),
      lastChangeSource(ChangeSource::None),
      isInitialSync(true),
      isUpdatingFromWindows(true)
{
    float currentVolume = windowsManager.GetVolume();
    lastWindowsState.volume = (currentVolume >= 0.0f) ? currentVolume : 0.0f;

    bool isMuted = windowsManager.GetMute();
    lastWindowsState.isMuted = isMuted;

    float vmVolume = 0.0f;
    bool vmMute = false;
    if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
        lastVmState.volume = vmVolume;
        lastVmState.isMuted = vmMute;
    } else {
        lastVmState.volume = 0.0f;
        lastVmState.isMuted = false;
    }

    LOG_DEBUG("[VolumeMirror::VolumeMirror] Initialized with Windows volume: " + std::to_string(lastWindowsState.volume) +
              "%, mute: " + std::string(lastWindowsState.isMuted ? "Muted" : "Unmuted") + 
              ", Voicemeeter volume: " + std::to_string(lastVmState.volume) + "%, mute: " + (lastVmState.isMuted ? "Muted" : "Unmuted"));

    // Register the Windows volume change callback
    windowsVolumeCallback = [this](float newVolume, bool isMuted) {
        this->OnWindowsVolumeChange(newVolume, isMuted);
    };
    windowsManager.RegisterVolumeChangeCallback(windowsVolumeCallback);
}

// Destructor
VolumeMirror::~VolumeMirror() {
    Stop();

    // Unregister the Windows volume change callback
    windowsManager.UnregisterVolumeChangeCallback(windowsVolumeCallback);

    // Join the monitoring thread if it's joinable
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    LOG_DEBUG("[VolumeMirror::~VolumeMirror] VolumeMirror destroyed.");
}

// Start the volume mirroring
void VolumeMirror::Start() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (running.load())
        return;

    running.store(true);

    if (pollingEnabled) {
        monitorThread = std::thread(&VolumeMirror::MonitorVoicemeeter, this);
        LOG_INFO("[VolumeMirror::Start] Polling mode started with interval: " +
                 std::to_string(pollingInterval) + "ms");
    }

    LOG_DEBUG("[VolumeMirror::Start] Volume mirroring started.");
}

// Stop the volume mirroring
void VolumeMirror::Stop() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!running.load())
        return;

    running.store(false);

    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    LOG_INFO("[VolumeMirror::Stop] Volume mirroring stopped.");
}

// Set polling mode and interval
void VolumeMirror::SetPollingMode(bool enabled, int interval) {
    pollingEnabled = enabled;
    pollingInterval = interval;

    LOG_INFO("[VolumeMirror::SetPollingMode] Polling mode " +
             std::string(enabled ? "enabled"
                                 : "disabled - Sync is one-way from Windows to Voicemeeter.") +
             (enabled ? " with interval: " + std::to_string(pollingInterval) + "ms"
                      : ""));
}

// Callback function for Windows volume changes
void VolumeMirror::OnWindowsVolumeChange(float newVolume, bool isMuted) {
    if (ignoreWindowsChange)
        return;

    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(vmMutex);

    if (now < suppressWindowsUntil) {
        LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Ignoring Windows volume change within suppression window.");
        return;
    }

    // Set or update the pending change
    pendingWindowsChange = std::make_pair(newVolume, isMuted);
    debounceTimerStart = now;

    LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Pending Windows volume change set to " + std::to_string(newVolume) +
              "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
}

// MonitorVoicemeeter method with debouncing and sync sound suppression
void VolumeMirror::MonitorVoicemeeter() {
    bool pendingSound = false;

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));

        {
            std::lock_guard<std::mutex> lock(vmMutex);

            // Handle pending Windows volume change with debouncing
            if (pendingWindowsChange.has_value()) {
                auto now = std::chrono::steady_clock::now();
                if (now - debounceTimerStart >= std::chrono::milliseconds(debounceDuration)) {
                    float newVolume = pendingWindowsChange->first;
                    bool isMuted = pendingWindowsChange->second;
                    pendingWindowsChange.reset(); // Clear the pending change

                    VolumeState newState{newVolume, isMuted};
                    if (newState != lastWindowsState) {
                        lastWindowsState = newState;

                        isUpdatingFromWindows.store(true, std::memory_order_relaxed); // Indicate internal update
                        ignoreVoicemeeterChange = true;
                        vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, isMuted);
                        ignoreVoicemeeterChange = false;

                        suppressVoicemeeterUntil = now + std::chrono::milliseconds(suppressionDuration);

                        lastChangeSource = ChangeSource::Windows;

                        LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Processed debounced Windows volume change to " + std::to_string(newVolume) +
                                  "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
                    }
                }
            }

            // Check Voicemeeter parameters
            if (vmManager.IsParametersDirty()) {
                float vmVolume = 0.0f;
                bool vmMute = false;
                if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
                    LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Voicemeeter volume: " + std::to_string(vmVolume) + "%, mute: " + std::to_string(vmMute));

                    VolumeState currentVmState{vmVolume, vmMute};
                    bool stateChanged = (currentVmState != lastVmState);

                    if (vmVolume >= 0.0f && stateChanged) {
                        lastVmState = currentVmState;

                        auto now = std::chrono::steady_clock::now();
                        if (now < suppressVoicemeeterUntil) {
                            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Ignoring Voicemeeter volume change within suppression window.");
                            continue;
                        }

                        if (lastChangeSource == ChangeSource::Windows &&
                            VolumeUtils::IsFloatEqual(vmVolume, lastWindowsState.volume) &&
                            vmMute == lastWindowsState.isMuted) {
                            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Voicemeeter volume change matches last Windows volume change. Skipping update to Windows.");
                            continue;
                        }

                        if (isUpdatingFromWindows.load(std::memory_order_relaxed)) {
                            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Change originated from Windows. Suppressing sync sound.");
                            isUpdatingFromWindows.store(false, std::memory_order_relaxed); // Reset the flag
                            continue; // Do not play sync sound
                        }

                        ignoreWindowsChange = true;
                        UpdateWindowsVolume(vmVolume, vmMute);
                        ignoreWindowsChange = false;

                        suppressWindowsUntil = now + std::chrono::milliseconds(suppressionDuration);

                        lastChangeSource = ChangeSource::Voicemeeter;

                        // Handle initial synchronization
                        if (isInitialSync.load(std::memory_order_relaxed)) {
                            isInitialSync.store(false, std::memory_order_relaxed); // Mark initial sync as done
                            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Initial synchronization completed. Sync sound suppressed.");
                            continue; // Do not play sync sound during initial sync
                        }

                        // Play sync sound if enabled
                        if (playSoundOnSync && !VolumeUtils::IsFloatEqual(vmVolume, lastWindowsState.volume)) {
                            pendingSound = true;
                            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Voicemeeter volume change detected. Scheduling synchronization sound.");
                        }

                        lastChangeSource = ChangeSource::Voicemeeter;
                    }
                }
            }

            // Diagnostic Logging
            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] isUpdatingFromWindows: " + std::string(isUpdatingFromWindows.load() ? "True" : "False"));
            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] isInitialSync: " + std::string(isInitialSync.load() ? "True" : "False"));
            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] lastChangeSource: " + std::to_string(static_cast<uint8_t>(lastChangeSource)));
        }

        // Handle pending sync sound outside the lock to avoid blocking
        if (pendingSound) {
            auto now = std::chrono::steady_clock::now();
            // Ensure debounce period has passed
            if (now - debounceTimerStart >= std::chrono::milliseconds(debounceDuration)) {
                LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Playing sync sound after debounce period");
                windowsManager.PlaySyncSound();
                pendingSound = false;
            }
        }
    }
}

// Get the current Voicemeeter volume and mute state
bool VolumeMirror::GetVoicemeeterVolume(float& volumePercent, bool& isMuted) {
    return vmManager.GetVoicemeeterVolume(channelIndex, channelType, volumePercent, isMuted);
}

// Update Voicemeeter volume and mute state based on Windows volume
void VolumeMirror::UpdateVoicemeeterVolume(float volumePercent, bool isMuted) {
    vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, volumePercent, isMuted);
    LOG_DEBUG("[VolumeMirror::UpdateVoicemeeterVolume] Voicemeeter volume updated to " + std::to_string(volumePercent) + "% " +
              (isMuted ? "(Muted)" : "Unmuted)"));
}

// Update Windows volume and mute state based on Voicemeeter volume
void VolumeMirror::UpdateWindowsVolume(float volumePercent, bool isMuted) {
    try {
        if (!windowsManager.SetVolume(volumePercent)) {
            LOG_ERROR("[VolumeMirror::UpdateWindowsVolume] Failed to set Windows volume.");
        }

        if (!windowsManager.SetMute(isMuted)) {
            LOG_ERROR("[VolumeMirror::UpdateWindowsVolume] Failed to set Windows mute state.");
        }

        lastWindowsState.volume = volumePercent;
        lastWindowsState.isMuted = isMuted;

        suppressVoicemeeterUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(suppressionDuration);

        LOG_DEBUG("[VolumeMirror::UpdateWindowsVolume] Windows volume updated to " + std::to_string(volumePercent) + "% " +
                  (isMuted ? "(Muted)" : "Unmuted)"));
    } catch (const std::exception& ex) {
        LOG_ERROR("[VolumeMirror::UpdateWindowsVolume] Exception occurred: " + std::string(ex.what()));
    }
}