// VolumeMirror.cpp
#include "VolumeMirror.h"

#include <chrono>
#include <thread>

#include "Logger.h"  // For logging
#include "SoundManager.h"
#include "VolumeUtils.h"

using namespace VolumeUtils;

VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, VoicemeeterManager& manager, WindowsManager& windowsManager, Mode mode)
    : channelIndex(channelIdx),
      channelType(type),
      vmManager(manager),
      windowsManager(windowsManager),
      mode(mode),
      running(false),
      pollingInterval(DEFAULT_POLLING_INTERVAL_MS),
      updatingVoicemeeter(false),
      updatingWindows(false),
      lastWinVolume(0.0f),
      lastWinMute(false),
      lastVmVolume(0.0f),
      lastVmMute(false),
      pendingVmVolume(0.0f),
      pendingVmMute(false),
      vmChangePending(false) {
    LOG_DEBUG("[VolumeMirror::Constructor] Initializing VolumeMirror.");

    // Initial synchronization: Set Voicemeeter volume to match Windows
    lastWinVolume = windowsManager.GetVolume();
    lastWinMute = windowsManager.GetMute();

    // Round the initial Windows volume
    lastWinVolume = std::round(lastWinVolume * 100.0f) / 100.0f;

    LOG_DEBUG("[VolumeMirror::Constructor] Fetched Initial Windows Volume: " + std::to_string(lastWinVolume) + "%, Mute: " + (lastWinMute ? "Muted" : "Unmuted"));

    vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, lastWinVolume, lastWinMute);
    LOG_INFO("[VolumeMirror::Constructor] Voicemeeter volume and mute state synchronized with Windows.");

    lastVmVolume = lastWinVolume;
    lastVmMute = lastWinMute;

    if (mode == Mode::Callback || mode == Mode::Hybrid) {
        LOG_DEBUG("[VolumeMirror::Constructor] Registering Windows Volume Change Callback.");
        windowsVolumeCallback = [this](float newVolume, bool isMuted) {
            this->OnWindowsVolumeChange(newVolume, isMuted);
        };
        windowsVolumeCallbackID = windowsManager.RegisterVolumeChangeCallback(windowsVolumeCallback);
        LOG_DEBUG("[VolumeMirror::Constructor] Windows Volume Change Callback registered with ID: " + std::to_string(windowsVolumeCallbackID));
    }
}

VolumeMirror::~VolumeMirror() {
    LOG_DEBUG("[VolumeMirror::~Destructor] Stopping VolumeMirror.");
    Stop();

    if (mode == Mode::Callback || mode == Mode::Hybrid) {
        LOG_DEBUG("[VolumeMirror::~Destructor] Unregistering Windows Volume Change Callback with ID: " + std::to_string(windowsVolumeCallbackID));
        windowsManager.UnregisterVolumeChangeCallback(windowsVolumeCallbackID);
        LOG_DEBUG("[VolumeMirror::~Destructor] Windows Volume Change Callback unregistered.");
    }

    LOG_DEBUG("[VolumeMirror::~Destructor] Cleanup complete.");
}

void VolumeMirror::Start() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (running.load()) {
        LOG_DEBUG("[VolumeMirror::Start] Start called, but VolumeMirror is already running.");
        return;
    }

    running.store(true);
    LOG_DEBUG("[VolumeMirror::Start] VolumeMirror started.");

    if (mode == Mode::Polling || mode == Mode::Hybrid) {
        LOG_DEBUG("[VolumeMirror::Start] Starting MonitorVolumes thread in Polling/Hybrid mode.");
        monitorThread = std::thread(&VolumeMirror::MonitorVolumes, this);
        LOG_DEBUG("[VolumeMirror::Start] MonitorVolumes thread started.");
    }

    LOG_INFO("[VolumeMirror::Start] VolumeMirror is now running.");
}

void VolumeMirror::Stop() {
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!running.load()) {
        LOG_DEBUG("[VolumeMirror::Stop] Stop called, but VolumeMirror is not running.");
        return;
    }

    running.store(false);
    LOG_DEBUG("[VolumeMirror::Stop] VolumeMirror stopping...");

    if (monitorThread.joinable()) {
        LOG_DEBUG("[VolumeMirror::Stop] Joining MonitorVolumes thread.");
        monitorThread.join();
        LOG_DEBUG("[VolumeMirror::Stop] MonitorVolumes thread joined.");
    }

    LOG_INFO("[VolumeMirror::Stop] VolumeMirror has been stopped.");
}

void VolumeMirror::OnWindowsVolumeChange(float newVolume, bool isMuted) {
    LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Triggered. New Volume: " + std::to_string(newVolume) + "%, Mute: " + (isMuted ? "Muted" : "Unmuted"));

    std::lock_guard<std::mutex> lock(controlMutex);

    if (updatingWindows) {
        LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Currently updating Windows volume. Skipping to prevent recursive updates.");
        return;
    }

    // Round the new volume to two decimal places
    newVolume = std::round(newVolume * 100.0f) / 100.0f;

    if (!IsFloatEqual(newVolume, lastWinVolume) || isMuted != lastWinMute) {
        LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Detected change in Windows Volume or Mute state.");
        LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Previous Windows Volume: " + std::to_string(lastWinVolume) + "%, Previous Mute: " + (lastWinMute ? "Muted" : "Unmuted"));

        lastWinVolume = newVolume;
        lastWinMute = isMuted;

        if (!IsFloatEqual(newVolume, lastVmVolume) || isMuted != lastVmMute) {
            LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] Updating Voicemeeter Volume and Mute state to match Windows.");
            updatingVoicemeeter = true;
            vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, isMuted);
            updatingVoicemeeter = false;

            LOG_INFO("[VolumeMirror::OnWindowsVolumeChange] Voicemeeter volume and mute state synchronized with Windows.");

            lastVmVolume = newVolume;
            lastVmMute = isMuted;
        }
    } else {
        LOG_DEBUG("[VolumeMirror::OnWindowsVolumeChange] No significant change in Windows volume or mute state. Skipping update.");
    }
}

void VolumeMirror::MonitorVolumes() {
    LOG_DEBUG("[VolumeMirror::MonitorVolumes] Thread started.");

    while (running.load()) {
        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Polling cycle started.");
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));
        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Polling interval elapsed.");

        std::lock_guard<std::mutex> lock(controlMutex);

        // Poll Voicemeeter
        float vmVolume = 0.0f;
        bool vmMute = false;

        if (vmManager.GetVoicemeeterVolume(channelIndex, channelType, vmVolume, vmMute)) {
            // Round the Voicemeeter volume
            vmVolume = std::round(vmVolume * 100.0f) / 100.0f;

            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Fetched Voicemeeter Volume: " + std::to_string(vmVolume) + "%, Mute: " + (vmMute ? "Muted" : "Unmuted"));
        } else {
            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Failed to fetch Voicemeeter Volume");
            return;
        }

        if (!IsFloatEqual(vmVolume, lastVmVolume) || vmMute != lastVmMute) {
            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Detected change in Voicemeeter Volume or Mute state.");
            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Previous Voicemeeter Volume: " + std::to_string(lastVmVolume) + "%, Previous Mute: " + (lastVmMute ? "Muted" : "Unmuted"));
            LOG_DEBUG("[VolumeMirror::MonitorVolumes] New Voicemeeter Volume: " + std::to_string(vmVolume) + "%, New Mute: " + (vmMute ? "Muted" : "Unmuted"));

            if (!updatingVoicemeeter) {
                // Debounce Logic: Confirm the change in the next polling cycle
                if (!vmChangePending) {
                    // First detection of change
                    pendingVmVolume = vmVolume;
                    pendingVmMute = vmMute;
                    vmChangePending = true;
                    LOG_DEBUG("[VolumeMirror::MonitorVolumes] Voicemeeter change detected. Awaiting confirmation in next polling cycle.");
                } else {
                    // Second detection: Check if the change is consistent
                    if (IsFloatEqual(vmVolume, pendingVmVolume) && vmMute == pendingVmMute) {
                        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Voicemeeter change confirmed. Updating Windows Volume and Mute state.");
                        updatingWindows = true;
                        windowsManager.SetVolume(vmVolume);
                        windowsManager.SetMute(vmMute);
                        updatingWindows = false;

                        LOG_INFO("[VolumeMirror::MonitorVolumes] Windows volume and mute state updated to match Voicemeeter.");

                        // Play sound on Voicemeeter -> Windows change
                        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Playing synchronization sound.");
                        SoundManager::Instance().PlaySyncSound();

                        // Update lastVmVolume and lastVmMute after confirmation
                        lastVmVolume = vmVolume;
                        lastVmMute = vmMute;

                        // Update lastWinVolume and lastWinMute as well
                        lastWinVolume = vmVolume;
                        lastWinMute = vmMute;

                        // Reset pending state
                        vmChangePending = false;
                    } else {
                        // Change was not consistent; reset pending state
                        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Voicemeeter volume changed again before confirmation. Resetting pending state.");
                        pendingVmVolume = vmVolume;
                        pendingVmMute = vmMute;
                        // vmChangePending remains true to await confirmation
                    }
                }
            }
        } else {
            // If no change detected and there was a pending change, reset the pending state
            if (vmChangePending) {
                LOG_DEBUG("[VolumeMirror::MonitorVolumes] No further change detected in Voicemeeter volume. Resetting pending state.");
                vmChangePending = false;
            }
        }

        // In Polling mode, also poll Windows
        if (mode == Mode::Polling) {
            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Mode is Polling. Checking Windows Volume and Mute state.");
            float winVolume = windowsManager.GetVolume();
            bool winMute = windowsManager.GetMute();

            // Round the Windows volume
            winVolume = std::round(winVolume * 100.0f) / 100.0f;

            LOG_DEBUG("[VolumeMirror::MonitorVolumes] Fetched Windows Volume: " + std::to_string(winVolume) + "%, Mute: " + (winMute ? "Muted" : "Unmuted"));

            if (!IsFloatEqual(winVolume, lastWinVolume) || winMute != lastWinMute) {
                LOG_DEBUG("[VolumeMirror::MonitorVolumes] Detected change in Windows Volume or Mute state.");

                if (!updatingWindows) {
                    if (!IsFloatEqual(winVolume, lastVmVolume) || winMute != lastVmMute) {
                        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Updating Voicemeeter Volume and Mute state to match Windows.");
                        updatingVoicemeeter = true;
                        vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, winVolume, winMute);
                        updatingVoicemeeter = false;

                        LOG_INFO("[VolumeMirror::MonitorVolumes] Voicemeeter volume and mute state synchronized with Windows.");

                        lastVmVolume = winVolume;
                        lastVmMute = winMute;
                    }
                }

                lastWinVolume = winVolume;
                lastWinMute = winMute;
            }
        }

        LOG_DEBUG("[VolumeMirror::MonitorVolumes] Polling cycle completed.");
    }

    LOG_DEBUG("[VolumeMirror::MonitorVolumes] Thread exiting.");
}
