#include "WindowsManager.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <chrono>

#include "Logger.h"
#include "VoicemeeterManager.h"

// Initialize COM library
bool WindowsManager::InitializeCOM() {
    HANDLE hMutex = CreateMutexA(NULL, FALSE, COM_INIT_MUTEX_NAME);
    if (hMutex) {
        WaitForSingleObject(hMutex, INFINITE);
        LOG_DEBUG("Attempting to initialize COM library");

        if (!comInitialized) {
            HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            if (SUCCEEDED(hr)) {
                comInitialized = true;
                LOG_DEBUG("COM library initialized successfully.");
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
                return true;
            } else if (hr == RPC_E_CHANGED_MODE) {
                LOG_DEBUG("COM already initialized with a different threading model.");
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
                return true;
            } else {
                LOG_ERROR("Failed to initialize COM library. HRESULT: " + std::to_string(hr));
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
                return false;
            }
        }
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return true;
}

// Uninitialize COM library
void WindowsManager::UninitializeCOM() {
    WaitForSingleObject(comInitMutex, INFINITE);
    if (!endpointVolume || !endpointVolume.Get()) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
        if (!endpointVolume || !endpointVolume.Get()) {
            ReleaseMutex(comInitMutex);
            return;
        }
    }
    if (comInitialized) {
        ::CoUninitialize();
        comInitialized = false;
        LOG_DEBUG("COM library uninitialized successfully.");
    }
}

// Constructor: Initialize COM, volume control, and device monitoring
WindowsManager::WindowsManager(const std::string& deviceUUID, ToggleConfig toggleConfig, VoicemeeterManager& manager)
    : refCount(1),
      comInitMutex(CreateMutexA(NULL, FALSE, COM_INIT_MUTEX_NAME)),
      comInitialized(false),
      voicemeeterManager(manager),
      targetDeviceUUID(deviceUUID),
      toggleConfig(toggleConfig),
      isToggled(false) {
    // Initialize mutex first
    comInitMutex = CreateMutexA(NULL, FALSE, COM_INIT_MUTEX_NAME);
    if (!comInitMutex) {
        throw std::runtime_error("Failed to create COM init mutex");
    }

    // Then initialize COM and interfaces
    try {
        if (!InitializeCOM()) {
            throw std::runtime_error("COM initialization failed");
        }

        if (!InitializeCOMInterfaces()) {
            throw std::runtime_error("COM interfaces initialization failed");
        }

        HRESULT hr = endpointVolume->RegisterControlChangeNotify(this);
        if (FAILED(hr)) {
            throw std::runtime_error("Volume notification registration failed: " + std::to_string(hr));
        }

        LOG_DEBUG("WindowsManager initialized successfully for UUID: " + targetDeviceUUID);
    } catch (const std::exception& e) {
        LOG_ERROR("WindowsManager initialization failed: " + std::string(e.what()));
        Cleanup();
        throw;
    }
}

WindowsManager::~WindowsManager() {
    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
    }
    if (comInitMutex) {
        CloseHandle(comInitMutex);
    }
    Cleanup();
    UninitializeCOM();
}

void WindowsManager::Cleanup() {
    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
        endpointVolume.Reset();
    }
    speakers.Reset();
    deviceEnumerator.Reset();
}

// Device state change handling methods (from DeviceMonitor)
STDMETHODIMP WindowsManager::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    LOG_DEBUG("OnDeviceStateChanged called for Device ID: " + std::to_string(dwNewState));
    if (dwNewState == DEVICE_STATE_ACTIVE) {
        CheckDevice(pwstrDeviceId, true);
    } else if (dwNewState == DEVICE_STATE_DISABLED) {
        CheckDevice(pwstrDeviceId, false);
    }
    return S_OK;
}

void WindowsManager::CheckDevice(LPCWSTR deviceId, bool isAdded) {
    std::wstring ws(deviceId);
    int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.length()), NULL, 0, NULL, NULL);
    std::string deviceUUID(size_needed, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.length()), &deviceUUID[0], size_needed, NULL, NULL);

    LOG_DEBUG("Checking device UUID: " + deviceUUID);

    if (deviceUUID == targetDeviceUUID) {
        if (isAdded) {
            HandleDevicePluggedIn();
        } else {
            HandleDeviceUnplugged();
        }
    }
}

void WindowsManager::HandleDevicePluggedIn() {
    std::lock_guard<std::mutex> lock(voicemeeterManager.toggleMutex);
    LOG_INFO("Monitored device has been plugged in.");
    voicemeeterManager.RestartAudioEngine();

    if (!toggleConfig.type.empty()) {
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2, true);
    }
}

void WindowsManager::HandleDeviceUnplugged() {
    std::lock_guard<std::mutex> lock(voicemeeterManager.toggleMutex);
    LOG_INFO("Monitored device has been unplugged.");

    if (!toggleConfig.type.empty()) {
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2, false);
    }
}

STDMETHODIMP WindowsManager::OnDeviceAdded(LPCWSTR) {
    LOG_DEBUG("Device added");
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDeviceRemoved(LPCWSTR) {
    LOG_DEBUG("Device removed");
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) {
    LOG_DEBUG("Default device changed");
    return S_OK;
}

STDMETHODIMP WindowsManager::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
    // Implementation of property value change event
    LOG_DEBUG("Property value changed");
    return S_OK;
}

void WindowsManager::ToggleMute(const std::string& type, int index1, int index2, bool isPluggedIn) {
    ChannelType channelType;

    if (type == "input") {
        channelType = ChannelType::Input;
    } else if (type == "output") {
        channelType = ChannelType::Output;
    } else {
        LOG_ERROR("Invalid toggle type: " + type);
        return;
    }

    if (isPluggedIn) {
        voicemeeterManager.SetMute(index1, channelType, false);
        voicemeeterManager.SetMute(index2, channelType, true);
        LOG_INFO("Device Plugged: Unmuted " + type + ":" + std::to_string(index1) +
                 ", Muted " + type + ":" + std::to_string(index2));
        isToggled = true;
    } else {
        voicemeeterManager.SetMute(index1, channelType, true);
        voicemeeterManager.SetMute(index2, channelType, false);
        LOG_INFO("Device Unplugged: Muted " + type + ":" + std::to_string(index1) +
                 ", Unmuted " + type + ":" + std::to_string(index2));
        isToggled = false;
    }
}

// Initialize COM interfaces
bool WindowsManager::InitializeCOMInterfaces() {
    HRESULT hr;

    // Create device enumerator with proper type casting
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(deviceEnumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("MMDeviceEnumerator creation failed: " + std::to_string(hr));
        return false;
    }

    // Get default audio endpoint
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr)) {
        LOG_ERROR("Default audio endpoint retrieval failed: " + std::to_string(hr));
        return false;
    }

    // Activate audio endpoint volume interface with proper type casting
    hr = speakers->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_INPROC_SERVER,
        nullptr,
        reinterpret_cast<void**>(endpointVolume.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("Audio endpoint volume activation failed: " + std::to_string(hr));
        return false;
    }

    return true;
}

bool WindowsManager::SetVolume(float volumePercent) {
    try {
        WaitForSingleObject(comInitMutex, INFINITE);
        if (!endpointVolume || !endpointVolume.Get()) {
            const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
            if (!endpointVolume || !endpointVolume.Get()) {
                ReleaseMutex(comInitMutex);
                return false;
            }
        }

        if (!endpointVolume) {
            ReinitializeCOMInterfaces();
        }

        if (volumePercent < 0.0f || volumePercent > 100.0f) {
            LOG_ERROR("Invalid volume percentage: " + std::to_string(volumePercent));
            return false;
        }

        float scalar = VolumeUtils::PercentToScalar(volumePercent);
        HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(scalar, nullptr);

        if (FAILED(hr)) {
            LOG_ERROR("Volume setting failed: " + std::to_string(hr));
            return false;
        }

        LOG_DEBUG("Volume set to " + std::to_string(volumePercent) + "%");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SetVolume exception: " + std::string(e.what()));
        return false;
    }
}

bool WindowsManager::SetMute(bool mute) {
    try {
        WaitForSingleObject(comInitMutex, INFINITE);
        if (!endpointVolume || !endpointVolume.Get()) {
            const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
            if (!endpointVolume || !endpointVolume.Get()) {
                ReleaseMutex(comInitMutex);
                return false;
            }
        }

        if (!endpointVolume) {
            ReinitializeCOMInterfaces();
        }

        HRESULT hr = endpointVolume->SetMute(mute, nullptr);
        if (FAILED(hr)) {
            LOG_ERROR("SetMute failed: " + std::to_string(hr));
            return false;
        }

        LOG_DEBUG("Mute state set to: " + std::string(mute ? "muted" : "unmuted"));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SetMute exception: " + std::string(e.what()));
        return false;
    }
}

float WindowsManager::GetVolume() const {
    try {
        if (!comInitMutex) {
            LOG_ERROR("COM init mutex is null");
            return -1.0f;
        }
        WaitForSingleObject(comInitMutex, INFINITE);
        if (!endpointVolume || !endpointVolume.Get()) {
            const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
            if (!endpointVolume || !endpointVolume.Get()) {
                ReleaseMutex(comInitMutex);
                return -1.0f;
            }
        }

        if (!endpointVolume || !endpointVolume.Get()) {
            const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
            if (!endpointVolume || !endpointVolume.Get()) {
                return -1.0f;
            }
        }

        float currentVolume = 0.0f;
        HRESULT hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);

        if (FAILED(hr)) {
            LOG_ERROR("GetVolume failed: " + std::to_string(hr));
            return -1.0f;
        }

        return VolumeUtils::ScalarToPercent(currentVolume);
    } catch (const std::exception& e) {
        LOG_ERROR("GetVolume exception: " + std::string(e.what()));
        return -1.0f;
    }
}
  void WindowsManager::ReinitializeCOMInterfaces() {
      ReleaseMutex(comInitMutex);
      Cleanup();
      LOG_DEBUG("Reinitializing COM interfaces");

      if (!InitializeCOMInterfaces()) {
          WaitForSingleObject(comInitMutex, INFINITE);
          throw std::runtime_error("COM interface reinitialization failed");
      }
      WaitForSingleObject(comInitMutex, INFINITE);
  }

// IUnknown implementation
STDMETHODIMP WindowsManager::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        AddRef();
        *ppvObject = static_cast<IAudioEndpointVolumeCallback*>(this);
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
WindowsManager::AddRef() {
    return ++refCount;  // Use pre-increment operator
}

STDMETHODIMP_(ULONG)
WindowsManager::Release() {
    ULONG ulRef = --refCount;  // Use pre-decrement operator
    if (ulRef == 0) {
        delete this;
    }
    return ulRef;
}

STDMETHODIMP WindowsManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) return E_POINTER;
    WaitForSingleObject(comInitMutex, INFINITE);
    if (!endpointVolume || !endpointVolume.Get()) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
        if (!endpointVolume || !endpointVolume.Get()) {
            ReleaseMutex(comInitMutex);
            return false;
        }
    }

    try {
        if (!deviceEnumerator || !speakers || !endpointVolume) {
            LOG_DEBUG("COM interfaces invalid, attempting reinitialization");
            ReinitializeCOMInterfaces();
            if (!deviceEnumerator || !speakers || !endpointVolume) {
                return E_FAIL;
            }
        }

        float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
        bool newMute = (pNotify->bMuted != FALSE);

        std::vector<std::function<void(float, bool)>> callbacksCopy;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbacksCopy = callbacks;
        }

        for (const auto& callback : callbacksCopy) {
            if (callback) {
                callback(newVolume, newMute);
            }
        }
        return S_OK;
    } catch (...) {
        LOG_ERROR("Exception in OnNotify");
        return E_UNEXPECTED;
    }
}

bool WindowsManager::GetMute() const {
    try {
        if (!endpointVolume) {
            const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
        }

        BOOL muted;
        HRESULT hr = endpointVolume->GetMute(&muted);

        if (FAILED(hr)) {
            LOG_ERROR("GetMute failed: " + std::to_string(hr));
            return false;
        }

        return muted != FALSE;
    } catch (const std::exception& e) {
        LOG_ERROR("GetMute exception: " + std::string(e.what()));
        return false;
    }
}

void WindowsManager::RegisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.push_back(callback);
}

void WindowsManager::UnregisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.erase(
        std::remove_if(callbacks.begin(), callbacks.end(),
                       [&callback](const auto& cb) {
                           return cb.target_type() == callback.target_type();
                       }),
        callbacks.end());
}
