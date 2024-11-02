// VolumeMirror.cpp
#include "VolumeMirror.h"

#include <windows.h>
#include <sapi.h>  // For PlaySound
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "Logger.h"
#include "VolumeUtils.h"
#include "VoicemeeterManager.h"

using Microsoft::WRL::ComPtr;

// Constructor
VolumeMirror::VolumeMirror(int channelIdx, VolumeUtils::ChannelType type, float minDbmVal,
                           float maxDbmVal, VoicemeeterManager &manager,
                           bool playSound)
    : channelIndex(channelIdx),
      channelType(type),
      minDbm(minDbmVal),
      maxDbm(maxDbmVal),
      vmManager(manager),
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
      pollingInterval(100),
      lastSoundPlayTime(std::chrono::steady_clock::now() - std::chrono::milliseconds(110)),
      debounceDuration(500) {

    // **Invoke COM Initialization via VoicemeeterManager**
    if (!vmManager.InitializeCOM()) {
        throw std::runtime_error("COM initialization failed via VoicemeeterManager.");
    }

    HRESULT hr;

    // Initialize Windows Audio Components
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                          __uuidof(IMMDeviceEnumerator), &deviceEnumerator);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create MMDeviceEnumerator.");
    }

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr)) {
        deviceEnumerator.Reset();
        throw std::runtime_error("Failed to get default audio endpoint.");
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                            NULL, &endpointVolume);
    if (FAILED(hr)) {
        speakers.Reset();
        deviceEnumerator.Reset();
        throw std::runtime_error("Failed to get IAudioEndpointVolume.");
    }

    // Register for volume change notifications
    hr = endpointVolume->RegisterControlChangeNotify(this);
    if (FAILED(hr)) {
        endpointVolume.Reset();
        speakers.Reset();
        deviceEnumerator.Reset();
        throw std::runtime_error("Failed to register for volume change notifications.");
    }

    // Get initial Windows volume and mute state
    float currentVolume = 0.0f;
    hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (SUCCEEDED(hr)) {
        lastWindowsVolume = VolumeUtils::ScalarToPercent(currentVolume);
    } else {
        lastWindowsVolume = 0.0f;
    }

    BOOL isMutedBOOL = FALSE;
    hr = endpointVolume->GetMute(&isMutedBOOL);
    if (SUCCEEDED(hr)) {
        lastWindowsMute = (isMutedBOOL != FALSE);
    } else {
        lastWindowsMute = false;
    }

    // Retrieve initial Voicemeeter volume and mute state from VoicemeeterManager
    float vmVolume = vmManager.GetChannelVolume(channelIndex, channelType);
    bool vmMute = vmManager.IsChannelMuted(channelIndex, channelType);

    if (vmVolume >= 0.0f) { // Valid volume retrieved
        lastVmVolume = vmVolume;
        lastVmMute = vmMute;
    } else {
        lastVmVolume = 0.0f;
        lastVmMute = false;
    }

    LOG_DEBUG("VolumeMirror initialized successfully.");
}

// Destructor
VolumeMirror::~VolumeMirror() {
    Stop();

    // Unregister for volume change notifications
    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
    }

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
    lastSoundPlayTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(pollingInterval + 10);

    LOG_INFO("Polling mode " +
             std::string(enabled ? "enabled"
                                 : "disabled -  Sync is one-way from Windows to "
                                   "Voicemeeter.") +
             (enabled ? " with interval: " + std::to_string(pollingInterval) + "ms"
                      : ""));
}

// IUnknown methods
STDMETHODIMP VolumeMirror::QueryInterface(REFIID riid, void **ppvObject) {
    if (IID_IUnknown == riid || __uuidof(IAudioEndpointVolumeCallback) == riid) {
        *ppvObject = static_cast<IUnknown *>(this);
        AddRef();
        return S_OK;
    } else {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_(ULONG)
VolumeMirror::AddRef() { return ++refCount; }

STDMETHODIMP_(ULONG)
VolumeMirror::Release() {
    ULONG ulRef = --refCount;
    if (0 == ulRef) {
        delete this;
    }
    return ulRef;
}

// OnNotify implementation - called when Windows volume changes
STDMETHODIMP VolumeMirror::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    // Check if the volume change originated from our own process
    if (pNotify->guidEventContext == eventContextGuid) {
        LOG_DEBUG("Ignored volume change notification from our own process.");
        return S_OK;
    }

    if (ignoreWindowsChange)
        return S_OK;

    std::lock_guard<std::mutex> lock(vmMutex);

    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);

    // If Windows volume or mute state has changed
    if (!VolumeUtils::IsFloatEqual(newVolume, lastWindowsVolume) ||
        newMute != lastWindowsMute) {
        lastWindowsVolume = newVolume;
        lastWindowsMute = newMute;

        // Update Voicemeeter volume accordingly
        ignoreVoicemeeterChange = true;
        vmManager.UpdateVoicemeeterVolume(channelIndex, channelType, newVolume, newMute);
        ignoreVoicemeeterChange = false;
    }

    return S_OK;
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

            float vmVolume = vmManager.GetChannelVolume(channelIndex, channelType);
            bool vmMute = vmManager.IsChannelMuted(channelIndex, channelType);

            if (vmVolume >= 0.0f && 
                (!VolumeUtils::IsFloatEqual(vmVolume, lastVmVolume) || vmMute != lastVmMute)) {
                lastVmVolume = vmVolume;
                lastVmMute = vmMute;

                // Update Windows volume accordingly
                ignoreWindowsChange = true;
                UpdateWindowsVolume(vmVolume, vmMute);
                ignoreWindowsChange = false;

                // Mark that a change has occurred and reset the timer
                lastVmChangeTime = std::chrono::steady_clock::now();
                pendingSound = true;

                LOG_DEBUG("Voicemeeter volume change detected. Scheduling synchronization sound.");
            }
        }

        // Check if the debounce period has passed since the last change
        if (pendingSound) {
            auto now = std::chrono::steady_clock::now();
            auto durationSinceLastChange = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVmChangeTime);

            if (durationSinceLastChange >= debounceDuration) {
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
    float scalarValue = VolumeUtils::PercentToScalar(volumePercent);

    HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(scalarValue,
                                                            &eventContextGuid);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set Windows master volume.");
    }

    hr = endpointVolume->SetMute(isMuted, &eventContextGuid);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set Windows mute state.");
    }

    LOG_DEBUG("Windows volume updated: " + std::to_string(volumePercent) + "% " +
              (isMuted ? "(Muted)" : "(Unmuted)"));
}

// Play synchronization sound
void VolumeMirror::PlaySyncSound() {
    std::lock_guard<std::mutex> lock(soundMutex);
    auto now = std::chrono::steady_clock::now();

    const std::wstring soundFilePath = L"C:\\Windows\\Media\\Windows Unlock.wav";
    BOOL played = PlaySoundW(soundFilePath.c_str(), NULL, SND_FILENAME | SND_ASYNC);

    if (!played) {
        LOG_DEBUG("Failed to play custom sound, trying fallback");
        // Always use the system asterisk sound as fallback
        played = PlaySound(TEXT("SystemAsterisk"), NULL, SND_ALIAS | SND_ASYNC);
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
