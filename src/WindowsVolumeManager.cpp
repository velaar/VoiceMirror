// WindowsVolumeManager.cpp
#include "WindowsVolumeManager.h"

namespace VolumeUtils {

// Constructor
WindowsVolumeManager::WindowsVolumeManager()
    : refCount(1) { // Initialize reference count
    if (!InitializeCOMInterfaces()) {
        throw std::runtime_error("Failed to initialize WindowsVolumeManager COM interfaces.");
    }

    // Register this instance as a volume change callback
    HRESULT hr = endpointVolume->RegisterControlChangeNotify(this);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to register for volume change notifications.");
    }

    LOG_DEBUG("WindowsVolumeManager initialized and registered for volume change notifications.");
}

// Destructor
WindowsVolumeManager::~WindowsVolumeManager() {
    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
    }
    LOG_DEBUG("WindowsVolumeManager unregistered from volume change notifications.");
}

// Initialize COM interfaces
bool WindowsVolumeManager::InitializeCOMInterfaces() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator), &deviceEnumerator);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create MMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return false;
    }

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get default audio endpoint. HRESULT: " + std::to_string(hr));
        deviceEnumerator.Reset();
        return false;
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                            NULL, &endpointVolume);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to activate IAudioEndpointVolume. HRESULT: " + std::to_string(hr));
        speakers.Reset();
        deviceEnumerator.Reset();
        return false;
    }

    return true;
}

// SetVolume implementation
bool WindowsVolumeManager::SetVolume(float volumePercent) {
    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        LOG_ERROR("Volume percent must be between 0 and 100");
        return false;
    }

    float scalarValue = VolumeUtils::PercentToScalar(volumePercent);
    HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(scalarValue, NULL);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set Windows master volume. HRESULT: " + std::to_string(hr));
        return false;
    }

    LOG_DEBUG("Windows volume set to " + std::to_string(volumePercent) + "%");
    return true;
}

// SetMute implementation
bool WindowsVolumeManager::SetMute(bool mute) {
    HRESULT hr = endpointVolume->SetMute(mute, NULL);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set Windows mute state. HRESULT: " + std::to_string(hr));
        return false;
    }

    LOG_DEBUG(std::string("Windows mute state set to ") + (mute ? "Muted" : "Unmuted"));
    return true;
}

// GetVolume implementation
float WindowsVolumeManager::GetVolume() const {
    float currentVolume = 0.0f;
    HRESULT hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (SUCCEEDED(hr)) {
        float volumePercent = VolumeUtils::ScalarToPercent(currentVolume);
        LOG_DEBUG("Retrieved Windows volume: " + std::to_string(volumePercent) + "%");
        return volumePercent;
    }
    LOG_ERROR("Failed to get Windows master volume. HRESULT: " + std::to_string(hr));
    return -1.0f;
}

// GetMute implementation
bool WindowsVolumeManager::GetMute() const {
    BOOL isMutedBOOL = FALSE;
    HRESULT hr = endpointVolume->GetMute(&isMutedBOOL);
    if (SUCCEEDED(hr)) {
        bool isMuted = (isMutedBOOL != FALSE);
        LOG_DEBUG(std::string("Retrieved Windows mute state: ") + (isMuted ? "Muted" : "Unmuted"));
        return isMuted;
    }
    LOG_ERROR("Failed to get Windows mute state. HRESULT: " + std::to_string(hr));
    return false;
}

// RegisterVolumeChangeCallback implementation
void WindowsVolumeManager::RegisterVolumeChangeCallback(const std::function<void(float, bool)>& callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.push_back(callback);
    LOG_DEBUG("Registered a new volume change callback.");
}

// UnregisterVolumeChangeCallback implementation
void WindowsVolumeManager::UnregisterVolumeChangeCallback(const std::function<void(float, bool)>& callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.erase(
        std::remove_if(callbacks.begin(), callbacks.end(),
                       [&](const std::function<void(float, bool)>& registeredCallback) {
                           // Note: std::function doesn't support direct comparison
                           // Users should manage their callbacks accordingly
                           return false; // Placeholder: Implement comparison if needed
                       }),        callbacks.end());
    LOG_DEBUG("Unregistered a volume change callback.");
}

// IUnknown methods
STDMETHODIMP WindowsVolumeManager::QueryInterface(REFIID riid, void** ppvObject) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppvObject = static_cast<IAudioEndpointVolumeCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) WindowsVolumeManager::AddRef() {
    return refCount.fetch_add(1) + 1;
}

STDMETHODIMP_(ULONG) WindowsVolumeManager::Release() {
    ULONG res = refCount.fetch_sub(1) - 1;
    if (res == 0) {
        delete this;
    }
    return res;
}

// IAudioEndpointVolumeCallback::OnNotify implementation
STDMETHODIMP WindowsVolumeManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) {
        return E_POINTER;
    }

    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);

    LOG_DEBUG("Volume change detected: " + std::to_string(newVolume) + "%, " + (newMute ? "Muted" : "Unmuted"));

    // Invoke all registered callbacks
    std::lock_guard<std::mutex> lock(callbackMutex);
    for (const auto& callback : callbacks) {
        // It's safer to invoke callbacks outside of the lock if they might perform long operations
        // To prevent potential deadlocks, consider copying the callbacks vector
        // and invoking callbacks without holding the lock
        callback(newVolume, newMute);
    }

    return S_OK;
}

} // namespace VolumeUtils
