#define NOMINMAX // Prevent windows.h from defining min and max macros

#include "VolumeMirror.h"

#include <chrono>
#include <mutex>
#include <thread>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"

using namespace VolumeUtils;

// Constructor
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
      lastWindowsVolume(-1.0f),
      lastWindowsMute(false),
      lastVmVolume(-1.0f),
      lastVmMute(false),
      ignoreWindowsChange(false),
      ignoreVoicemeeterChange(false),
      running(false),
      refCount(1),
      playSoundOnSync(playSound),
      pollingEnabled(false),
      pollingInterval(DEFAULT_POLLING_INTERVAL_MS),
      debounceDuration(DEBOUNCE_DURATION_MS),
      suppressVoicemeeterUntil(std::chrono::steady_clock::time_point::min()),
      suppressWindowsUntil(std::chrono::steady_clock::time_point::min()),
      lastChangeSource(ChangeSource::None)
{
    float currentVolume = windowsManager.GetVolume();
    lastWindowsVolume = (currentVolume >= 0.0f) ? currentVolume : 0.0f;

    bool isMuted = windowsManager.GetMute();
    lastWindowsMute = isMuted;

    float vmVolume = 0.0f;
    bool vmMute = false;
    if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
        lastVmVolume = vmVolume;
        lastVmMute = vmMute;
    } else {
        lastVmVolume = 0.0f;
        lastVmMute = false;
    }

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

    LOG_DEBUG("VolumeMirror destroyed.");
}

// Start the volume mirroring
void VolumeMirror::Start() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (running)
        return;

    running = true;

    if (pollingEnabled) {
        // Start the monitoring thread
        monitorThread = std::thread(&VolumeMirror::MonitorVoicemeeter, this);
        LOG_INFO("Polling mode running with interval: " +
                 std::to_string(pollingInterval) + "ms");
    }

    LOG_DEBUG("VolumeMirror - mirroring started.");
}

// Stop the volume mirroring
void VolumeMirror::Stop() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!running)
        return;

    running = false;

    // Wait for the monitoring thread to finish
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    LOG_INFO("VolumeMirror stopped.");
}

// Set polling mode and interval
void VolumeMirror::SetPollingMode(bool enabled, int interval) {
    pollingEnabled = enabled;
    pollingInterval = interval;

    LOG_INFO("Polling mode " +
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
        LOG_DEBUG("Ignoring Windows volume change within suppression window.");
        return;
    }

    if (!VolumeUtils::IsFloatEqual(newVolume, lastWindowsVolume) ||
        isMuted != lastWindowsMute) {
        lastWindowsVolume = newVolume;
        lastWindowsMute = isMuted;

        ignoreVoicemeeterChange = true;
        vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, isMuted);
        ignoreVoicemeeterChange = false;

        // Set suppression window for Voicemeeter changes
        suppressVoicemeeterUntil = now + std::chrono::milliseconds(SUPPRESSION_DURATION_MS);

        lastChangeSource = ChangeSource::Windows;

        LOG_DEBUG("OnWindowsVolumeChange: Windows volume changed to " + std::to_string(newVolume) +
                  "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
    }
}

void VolumeMirror::MonitorVoicemeeter() {
    std::chrono::steady_clock::time_point lastVmChangeTime =
        std::chrono::steady_clock::now() - std::chrono::milliseconds(debounceDuration);
    bool pendingSound = false;
    bool initialSyncDone = false; // To prevent sync sound at startup

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));

        if (ignoreVoicemeeterChange)
            continue;

        if (vmManager.IsParametersDirty()) {
            std::lock_guard<std::mutex> lock(vmMutex);

            float vmVolume = 0.0f;
            bool vmMute = false;
            if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
                LOG_DEBUG("Voicemeeter volume: " + std::to_string(vmVolume) + "%, mute: " + std::to_string(vmMute));

                bool volumeChanged = !VolumeUtils::IsFloatEqual(vmVolume, lastVmVolume);
                bool muteChanged = (vmMute != lastVmMute);

                if (vmVolume >= 0.0f && (volumeChanged || muteChanged)) {
                    lastVmVolume = vmVolume;
                    lastVmMute = vmMute;

                    auto now = std::chrono::steady_clock::now();
                    if (now < suppressVoicemeeterUntil) {
                        LOG_DEBUG("Ignoring Voicemeeter volume change within suppression window.");
                        continue;
                    }

                    // Check if the change matches the last change from Windows
                    if (lastChangeSource == ChangeSource::Windows &&
                        VolumeUtils::IsFloatEqual(vmVolume, lastWindowsVolume) &&
                        vmMute == lastWindowsMute) {
                        LOG_DEBUG("Voicemeeter volume change matches last Windows volume change. Skipping update to Windows.");
                        // Do not update lastChangeSource here
                        continue;
                    }

                    ignoreWindowsChange = true;
                    UpdateWindowsVolume(vmVolume, vmMute);
                    ignoreWindowsChange = false;

                    // Set suppression window for Windows changes
                    suppressWindowsUntil = now + std::chrono::milliseconds(SUPPRESSION_DURATION_MS);

                    lastVmChangeTime = now;

                    if (!initialSyncDone) {
                        initialSyncDone = true;
                        continue; // Skip playing sound during initial sync
                    }

                    if (playSoundOnSync && !VolumeUtils::IsFloatEqual(vmVolume, lastWindowsVolume)) {
                        pendingSound = true;
                        LOG_DEBUG("Voicemeeter volume change detected. Scheduling synchronization sound.");
                    }

                    lastChangeSource = ChangeSource::Voicemeeter;
                }
            }
        }

        if (pendingSound) {
            auto now = std::chrono::steady_clock::now();
            if (now - lastVmChangeTime >= std::chrono::milliseconds(debounceDuration)) {
                LOG_DEBUG("Playing sync sound after debounce period");
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
    LOG_DEBUG("Voicemeeter volume updated: " + std::to_string(volumePercent) + "% " +
              (isMuted ? "(Muted)" : "(Unmuted)"));
}

// Update Windows volume and mute state based on Voicemeeter volume
void VolumeMirror::UpdateWindowsVolume(float volumePercent, bool isMuted) {
    try {
        if (!windowsManager.SetVolume(volumePercent)) {
            LOG_ERROR("VolumeMirror: Failed to set Windows volume.");
        }

        if (!windowsManager.SetMute(isMuted)) {
            LOG_ERROR("VolumeMirror: Failed to set Windows mute state.");
        }

        lastWindowsVolume = volumePercent;
        lastWindowsMute = isMuted;

        // Set suppression window for Voicemeeter changes
        suppressVoicemeeterUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(SUPPRESSION_DURATION_MS);

        LOG_DEBUG("VolumeMirror: Windows volume updated to " + std::to_string(volumePercent) + "% " +
                  (isMuted ? "(Muted)" : "(Unmuted)"));
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("VolumeMirror: Exception in UpdateWindowsVolume: ") + ex.what());
    }
}
