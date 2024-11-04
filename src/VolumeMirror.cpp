// VolumeMirror.cpp
#include "VolumeMirror.h"

#include <sapi.h>  // For PlaySound

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "Logger.h"
#include "VoicemeeterManager.h"
#include "VolumeUtils.h"
#include "WindowsManager.h"
#include "Defconf.h" 
#include "ConfigParser.h"

using namespace VolumeUtils;

// Constructor
VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal,
                           float maxDbmVal, VoicemeeterManager &manager,
                           bool playSound)
    : channelIndex(channelIdx),
      channelType(type),
      minDbm(minDbmVal),
      maxDbm(maxDbmVal),
      vmManager(manager),
      windowsVolumeManager(),
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
      pollingInterval(DEFAULT_POLLING_INTERVAL_MS), // Use default from Defconf.h
      lastSoundPlayTime(std::chrono::steady_clock::now() - std::chrono::milliseconds(DEFAULT_POLLING_INTERVAL_MS + 10)),
      debounceDuration(DEBOUNCE_DURATION_MS) { // Initialize from Defconf.h
    // Initialize VolumeMirror state
    // Get initial Windows volume and mute state using WindowsManager
    float currentVolume = windowsVolumeManager->GetVolume();
    if (currentVolume >= 0.0f) {  // Valid volume retrieved
        lastWindowsVolume = currentVolume;
    } else {
        lastWindowsVolume = 0.0f;
    }

    bool isMuted = windowsVolumeManager->GetMute();
    lastWindowsMute = isMuted;

    // Retrieve initial Voicemeeter volume and mute state from VoicemeeterManager
    float vmVolume = 0.0f;
    bool vmMute = false;
    if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
        lastVmVolume = vmVolume;
        lastVmMute = vmMute;
    } else {
        lastVmVolume = 0.0f;
        lastVmMute = false;
    }

}

// Destructor
VolumeMirror::~VolumeMirror() {
    Stop();

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
}// Set polling mode and interval
void VolumeMirror::SetPollingMode(bool enabled, int interval) {
    pollingEnabled = enabled;
    pollingInterval = interval;
    lastSoundPlayTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(interval + 10);

    LOG_INFO("Polling mode " +
             std::string(enabled ? "enabled"
                                 : "disabled -  Sync is one-way from Windows to "
                                   "Voicemeeter.") +
             (enabled ? " with interval: " + std::to_string(pollingInterval) + "ms"
                      : ""));
}

// Callback function for Windows volume changes
void VolumeMirror::OnWindowsVolumeChange(float newVolume, bool isMuted) {
    if (ignoreWindowsChange)
        return;

    std::lock_guard<std::mutex> lock(vmMutex);

    // If Windows volume or mute state has changed
    if (!VolumeUtils::IsFloatEqual(newVolume, lastWindowsVolume) ||
        isMuted != lastWindowsMute) {
        lastWindowsVolume = newVolume;
        lastWindowsMute = isMuted;

        // Update Voicemeeter volume accordingly
        ignoreVoicemeeterChange = true;
        vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, isMuted);
        ignoreVoicemeeterChange = false;

        // Set the flag to indicate the change originated from Windows
        changeFromWindows = true;
    }
}

// Monitor Voicemeeter parameters in a separate thread
void VolumeMirror::MonitorVoicemeeter() {
    std::chrono::steady_clock::time_point lastVmChangeTime =
        std::chrono::steady_clock::now() - std::chrono::milliseconds(debounceDuration);
    bool pendingSound = false;

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));

        if (ignoreVoicemeeterChange)
            continue;

        if (vmManager.IsParametersDirty()) {
            std::lock_guard<std::mutex> lock(vmMutex);

            float vmVolume = 0.0f;
            bool vmMute = false;
            if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
                LOG_DEBUG("Voicemeeter volume: " + std::to_string(vmVolume) + ", mute: " + std::to_string(vmMute));

                if (vmVolume >= 0.0f &&
                    (!VolumeUtils::IsFloatEqual(vmVolume, lastVmVolume) || vmMute != lastVmMute)) {
                    lastVmVolume = vmVolume;
                    lastVmMute = vmMute;

                    // Update Windows volume accordingly using WindowsManager
                    ignoreWindowsChange = true;
                    UpdateWindowsVolume(vmVolume, vmMute);
                    ignoreWindowsChange = false;

                    // Mark that a change has occurred and reset the timer
                    lastVmChangeTime = std::chrono::steady_clock::now();
                    pendingSound = true;

                    LOG_DEBUG("Voicemeeter volume change detected. Scheduling synchronization sound.");
                }
            }
        }

        // Check if the debounce period has passed since the last change
        if (pendingSound) {
            auto now = std::chrono::steady_clock::now();
            auto durationSinceLastChange = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVmChangeTime);

            if (durationSinceLastChange.count() >= debounceDuration) {
                LOG_DEBUG("Playing sync sound after debounce period");
                if (playSoundOnSync) {
                    PlaySyncSound();
                }
                pendingSound = false;
            }
        }
    }
}

// Get the current Voicemeeter volume and mute state
bool VolumeMirror::GetVoicemeeterVolume(float &volumePercent, bool &isMuted) {
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
        // Use WindowsManager to set volume and mute state
        if (!windowsVolumeManager->SetVolume(volumePercent)) {
            LOG_ERROR("VolumeMirror: Failed to set Windows volume.");
        }

        if (!windowsVolumeManager->SetMute(isMuted)) {
            LOG_ERROR("VolumeMirror: Failed to set Windows mute state.");
        }

        LOG_DEBUG("VolumeMirror: Windows volume updated to " + std::to_string(volumePercent) + "%" +
                  (isMuted ? " (Muted)" : " (Unmuted)"));
    } catch (const std::exception &ex) {
        LOG_ERROR(std::string("VolumeMirror: Exception in UpdateWindowsVolume: ") + ex.what());
    }
}

// Play synchronization sound
void VolumeMirror::PlaySyncSound() {
    std::lock_guard<std::mutex> lock(soundMutex);
    auto now = std::chrono::steady_clock::now();

    if (changeFromWindows.load()) {
        LOG_DEBUG("Change originated from Windows. Skipping PlaySound().");
        changeFromWindows = false;  // Reset the flag
        return;
    }

    // Use constants from Defconf.h
    BOOL played = PlaySoundW(SYNC_SOUND_FILE_PATH, NULL, SND_FILENAME | SND_ASYNC);

    if (!played) {
        LOG_DEBUG("Failed to play custom sound, trying fallback");
        // Always use the system asterisk sound as fallback
        played = PlaySoundW(SYNC_FALLBACK_SOUND_ALIAS, NULL, SND_ALIAS | SND_ASYNC);
        if (played) {
            LOG_DEBUG("Fallback sound played successfully");
        } else {
            LOG_ERROR("Both custom and fallback sounds failed to play");
        }
    } else {
        LOG_DEBUG("Custom sound played successfully");
    }

    lastSoundPlayTime = now;
}
