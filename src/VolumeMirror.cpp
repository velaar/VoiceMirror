#pragma once
#include "VolumeMirror.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#undef min

#include "VoicemeeterManager.h"
#include "VolumeUtils.h"
#include "WindowsManager.h"

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
      lastUpdateSource(UpdateSource::Voicemeeter),
      playSoundOnSync(playSound)  // Initialize play sound flag
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
        LOG_DEBUG("[VolumeMirror::windowsVolumeCallback] Invoked with newVolume: " + std::to_string(newVolume) +
                  "%, isMuted: " + std::string(isMuted ? "Muted" : "Unmuted"));
        this->OnWindowsVolumeChange(newVolume, isMuted);
    };
    windowsVolumeCallbackID = windowsManager.RegisterVolumeChangeCallback(windowsVolumeCallback);
    LOG_DEBUG("[VolumeMirror::VolumeMirror] Registered Windows volume change callback with ID: " + std::to_string(windowsVolumeCallbackID));
}

// Destructor
VolumeMirror::~VolumeMirror() {
    Stop();
    // Unregister the Windows volume change callback using CallbackID
    bool unregistered = windowsManager.UnregisterVolumeChangeCallback(windowsVolumeCallbackID);
    if (!unregistered) {
        LOG_ERROR("[VolumeMirror::~VolumeMirror] Failed to unregister Windows volume change callback with ID: " + std::to_string(windowsVolumeCallbackID));
    } else {
        LOG_DEBUG("[VolumeMirror::~VolumeMirror] Successfully unregistered Windows volume change callback with ID: " + std::to_string(windowsVolumeCallbackID));
    }

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
    LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Callback received with newVolume: " + std::to_string(newVolume) +
              "%, isMuted: " + std::string(isMuted ? "Muted" : "Unmuted"));

    vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, isMuted);
    lastUpdateSource = UpdateSource::Windows;
    LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Updated Voicemeeter volume to match Windows: Volume = " +
              std::to_string(newVolume) + "%, Mute = " + (isMuted ? "Muted" : "Unmuted"));
}

// MonitorVoicemeeter method with debouncing and sync sound suppression
void VolumeMirror::MonitorVoicemeeter() {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));

        float vmVolume = 0.0f;
        bool vmMute = false;

        if (vmManager.IsParametersDirty() &&
            vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
            LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Retrieved Voicemeeter volume: " + std::to_string(vmVolume) +
                      "%, mute: " + std::string(vmMute ? "Muted" : "Unmuted"));

            if (vmVolume != lastVmState.volume || vmMute != lastVmState.isMuted) {
                UpdateWindowsVolume(vmVolume, vmMute);
                LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Updated Windows volume from Voicemeeter: Volume = " +
                          std::to_string(vmVolume) + "%, Mute = " + (vmMute ? "Muted" : "Unmuted"));

                if (playSoundOnSync) {
                    if (lastUpdateSource != UpdateSource::Windows) {
                        SoundManager::Instance().PlaySyncSound();

                        LOG_DEBUG("[VolumeMirror::MonitorVoicemeeter] Played sync sound after updating Windows volume.");
                    }
                }

                lastUpdateSource = UpdateSource::Voicemeeter;
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

        LOG_DEBUG("[VolumeMirror::UpdateWindowsVolume] Windows volume updated to " + std::to_string(volumePercent) + "% " +
                  (isMuted ? "(Muted)" : "Unmuted)"));
    } catch (const std::exception& ex) {
        LOG_ERROR("[VolumeMirror::UpdateWindowsVolume] Exception occurred: " + std::string(ex.what()));
    }
}
