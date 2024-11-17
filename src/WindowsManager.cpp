// WindowsManager.cpp
#include "WindowsManager.h"

#include <functiondiscoverykeys_devpkey.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "SoundManager.h"
#include "VolumeUtils.h"

using Microsoft::WRL::ComPtr;

// Constructor
WindowsManager::WindowsManager(const Config& config)
    : config_(config),
      hotkeyModifiers_(config.hotkeyModifiers.value),
      hotkeyVK_(config.hotkeyVK.value),
      comInitialized_(false),
      hwndHotkeyWindow_(nullptr) {
    LOG_DEBUG("[WindowsManager::WindowsManager] Initializing WindowsManager with config values.");
    try {
        if (!InitializeCOM())
            throw std::runtime_error("COM initialization failed");
        if (!InitializeCOMInterfaces())
            throw std::runtime_error("COM interfaces initialization failed");

        HRESULT hr = endpointVolume_->RegisterControlChangeNotify(this);
        if (FAILED(hr))
            throw std::runtime_error("Volume notification registration failed");

        hr = deviceEnumerator_->RegisterEndpointNotificationCallback(this);
        if (FAILED(hr))
            throw std::runtime_error("Device notification registration failed");

        LOG_DEBUG("[WindowsManager::WindowsManager] Successfully registered volume and device notifications.");
        InitializeHotkey();
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[WindowsManager::WindowsManager] Initialization failed: ") + ex.what());
        Cleanup();
        UninitializeCOM();
        throw;
    }
}

// Destructor
WindowsManager::~WindowsManager() {
    LOG_DEBUG("[WindowsManager::~WindowsManager] Cleaning up WindowsManager resources.");
    CleanupHotkey();
    if (endpointVolume_) {
        endpointVolume_->UnregisterControlChangeNotify(this);
        LOG_DEBUG("[WindowsManager::~WindowsManager] Unregistered volume change notification.");
    }
    if (deviceEnumerator_) {
        deviceEnumerator_->UnregisterEndpointNotificationCallback(this);
        LOG_DEBUG("[WindowsManager::~WindowsManager] Unregistered device notification callback.");
    }
    Cleanup();
    UninitializeCOM();
}

// COM Initialization
bool WindowsManager::InitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex_);
    if (!comInitialized_) {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            comInitialized_ = true;
            return true;
        }
        return false;
    }
    return true;
}

void WindowsManager::UninitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex_);
    if (comInitialized_) {
        ::CoUninitialize();
        comInitialized_ = false;
    }
}

// COM Interface Initialization
bool WindowsManager::InitializeCOMInterfaces() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(deviceEnumerator_.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to create MMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return false;
    }

    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &speakers_);
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to get default audio endpoint. HRESULT: " + std::to_string(hr));
        return false;
    }

    hr = speakers_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(endpointVolume_.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to activate IAudioEndpointVolume. HRESULT: " + std::to_string(hr));
        return false;
    }

    LOG_DEBUG("[WindowsManager::InitializeCOMInterfaces] Successfully initialized COM interfaces.");
    return true;
}

void WindowsManager::Cleanup() {
    endpointVolume_.Reset();
    speakers_.Reset();
    deviceEnumerator_.Reset();
}

// Reinitialize COM Interfaces
void WindowsManager::ReinitializeCOMInterfaces() {
    std::lock_guard<std::mutex> lock(soundMutex_);
    Cleanup();
    if (!InitializeCOMInterfaces())
        throw std::runtime_error("COM interface reinitialization failed");

    HRESULT hr = endpointVolume_->RegisterControlChangeNotify(this);
    if (FAILED(hr))
        throw std::runtime_error("Failed to re-register volume change notification");
}

// Volume Control Methods
bool WindowsManager::SetVolume(float volumePercent) {
    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        LOG_WARNING("[WindowsManager::SetVolume] Invalid volume percentage: " + std::to_string(volumePercent));
        return false;
    }

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::SetVolume] endpointVolume_ not initialized; attempting reinitialization.");
        try {
            ReinitializeCOMInterfaces();
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("[WindowsManager::SetVolume] Failed to reinitialize COM interfaces: ") + ex.what());
            return false;
        }
    }

    float scalar = VolumeUtils::PercentToScalar(volumePercent);
    HRESULT hr = endpointVolume_->SetMasterVolumeLevelScalar(scalar, nullptr);
    LOG_DEBUG("[WindowsManager::SetVolume] Set volume to " + std::to_string(volumePercent) + "% (scalar: " + std::to_string(scalar) + "). Result: " + std::to_string(hr));
    return SUCCEEDED(hr);
}

bool WindowsManager::SetMute(bool mute) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::SetMute] endpointVolume_ not initialized; attempting reinitialization.");
        try {
            ReinitializeCOMInterfaces();
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("[WindowsManager::SetMute] Failed to reinitialize COM interfaces: ") + ex.what());
            return false;
        }
    }

    HRESULT hr = endpointVolume_->SetMute(mute, nullptr);
    LOG_DEBUG("[WindowsManager::SetMute] Set mute to " + std::string(mute ? "true" : "false") + ". Result: " + std::to_string(hr));
    return SUCCEEDED(hr);
}

float WindowsManager::GetVolume() const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::GetVolume] endpointVolume_ not initialized; attempting reinitialization.");
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }

    float currentVolume = 0.0f;
    HRESULT hr = endpointVolume_->GetMasterVolumeLevelScalar(&currentVolume);
    LOG_DEBUG("[WindowsManager::GetVolume] Current volume: " + std::to_string(VolumeUtils::ScalarToPercent(currentVolume)) + "% (scalar: " + std::to_string(currentVolume) + "). Result: " + std::to_string(hr));
    return SUCCEEDED(hr) ? VolumeUtils::ScalarToPercent(currentVolume) : -1.0f;
}

bool WindowsManager::GetMute() const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }

    BOOL muted = FALSE;
    HRESULT hr = endpointVolume_->GetMute(&muted);
    return SUCCEEDED(hr) ? (muted != FALSE) : false;
}

// Callback Registration
CallbackID WindowsManager::RegisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    CallbackID id = nextCallbackID_++;
    volumeChangeCallbacks_[id] = std::move(callback);
    return id;
}

// IAudioEndpointVolumeCallback Implementation
STDMETHODIMP WindowsManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) {
        LOG_ERROR("[WindowsManager::OnNotify] Received null notification data.");
        return E_POINTER;
    }

    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);

    LOG_DEBUG("[WindowsManager::OnNotify] Notification received. Volume: " + std::to_string(newVolume) + "%, Mute: " + (newMute ? "Muted" : "Unmuted"));

    if (std::abs(newVolume - previousVolume_) < 1.0f && newMute == previousMute_) {
        LOG_DEBUG("[WindowsManager::OnNotify] Change is below threshold, skipping update.");
        return S_OK;
    }

    previousVolume_ = newVolume;
    previousMute_ = newMute;

    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        for (const auto& [id, callback] : volumeChangeCallbacks_) {
            callback(newVolume, newMute);
        }
    }

    LOG_INFO("[WindowsManager::OnNotify] Volume changed to " + std::to_string(newVolume) + "%, Muted: " + (newMute ? "Yes" : "No"));

    return S_OK;
}

// IUnknown Methods
STDMETHODIMP WindowsManager::QueryInterface(REFIID riid, void** ppvInterface) {
    if (!ppvInterface) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback))
        *ppvInterface = static_cast<IAudioEndpointVolumeCallback*>(this);
    else if (riid == __uuidof(IMMNotificationClient))
        *ppvInterface = static_cast<IMMNotificationClient*>(this);
    else {
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG)
WindowsManager::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG)
WindowsManager::Release() {
    ULONG ulRef = refCount_.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (ulRef == 0)
        delete this;
    return ulRef;
}

STDMETHODIMP WindowsManager::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = VolumeUtils::ConvertWStringToString(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceStateChanged] Device ID: " + deviceId + ", New State: " + std::to_string(dwNewState) + ".");

    switch (dwNewState) {
        case DEVICE_STATE_ACTIVE:
            LOG_INFO("[WindowsManager::OnDeviceStateChanged] Device activated: " + deviceId);
            if (onDevicePluggedIn) onDevicePluggedIn();
            break;

        case DEVICE_STATE_DISABLED:
        case DEVICE_STATE_UNPLUGGED:
            LOG_INFO("[WindowsManager::OnDeviceStateChanged] Device deactivated: " + deviceId);
            if (onDeviceUnplugged) onDeviceUnplugged();
            break;

        case DEVICE_STATE_NOTPRESENT:
            LOG_INFO("[WindowsManager::OnDeviceStateChanged] Device not present: " + deviceId);
            if (onDeviceUnplugged) onDeviceUnplugged(); // Assuming you have such a callback
            break;

        default:
            LOG_DEBUG("[WindowsManager::OnDeviceStateChanged] Device state changed to an unhandled state.");
            break;
    }
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = VolumeUtils::ConvertWStringToString(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceAdded] Device added: " + deviceId + ".");
    // Handle device addition if needed
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = VolumeUtils::ConvertWStringToString(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceRemoved] Device removed: " + deviceId + ".");
    // Handle device removal if needed
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    std::wstring wsDeviceId(pwstrDefaultDeviceId);
    std::string deviceId = VolumeUtils::ConvertWStringToString(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDefaultDeviceChanged] Default device changed. Flow: " + std::to_string(flow) +
             ", Role: " + std::to_string(role) + ", Device ID: " + deviceId + ".");
    // Handle default device change if needed
    return S_OK;
}

STDMETHODIMP WindowsManager::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = VolumeUtils::ConvertWStringToString(wsDeviceId);
    LOG_INFO("[WindowsManager::OnPropertyValueChanged] Device ID: " + deviceId + ", Property Key: {" +
             std::to_string(key.fmtid.Data1) + ", " + std::to_string(key.pid) + "}.");
    // Handle property value change if needed
    return S_OK;
}

// Initialize Hotkey
bool WindowsManager::InitializeHotkey() {
    const wchar_t CLASS_NAME[] = L"VoiceMirrorHotkeyHiddenWindow";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowsManager::WindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    hwndHotkeyWindow_ = CreateWindowExW(0, CLASS_NAME, L"Hotkey Hidden Window", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, this);
    if (!hwndHotkeyWindow_)
        return false;

    if (SetWindowLongPtrW(hwndHotkeyWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)) == 0) {
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
        return false;
    }

    if (!RegisterHotKey(hwndHotkeyWindow_, 1, hotkeyModifiers_, hotkeyVK_)) {
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
        return false;
    }

    LOG_DEBUG("[WindowsManager::InitializeHotkey] Hotkey registered successfully.");
    return true;
}

// Cleanup Hotkey
void WindowsManager::CleanupHotkey() {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (hwndHotkeyWindow_) {
        UnregisterHotKey(hwndHotkeyWindow_, 1);
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
        LOG_DEBUG("[WindowsManager::CleanupHotkey] Hotkey unregistered and window destroyed.");
    }
}

// Window Procedure
LRESULT CALLBACK WindowsManager::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_HOTKEY) {
        WindowsManager* pThis = reinterpret_cast<WindowsManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (pThis)
            pThis->WindowProcCallback();
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void WindowsManager::WindowProcCallback() {
    LOG_INFO("[WindowsManager::WindowProcCallback] Hotkey pressed. Performing associated actions.");
    SoundManager::Instance().PlaySyncSound();  // Play sync sound directly
}

// List Monitorable Devices
void WindowsManager::ListMonitorableDevices() {
    std::lock_guard<std::mutex> lock(soundMutex_);

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to create MMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return;
    }

    ComPtr<IMMDeviceCollection> deviceCollection;
    hr = enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to enumerate audio endpoints. HRESULT: " + std::to_string(hr));
        return;
    }

    UINT deviceCount = 0;
    hr = deviceCollection->GetCount(&deviceCount);
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to get device count. HRESULT: " + std::to_string(hr));
        return;
    }

    if (deviceCount == 0) {
        LOG_INFO("[WindowsManager::ListMonitorableDevices] No active audio devices found.");
        return;
    }

    constexpr size_t INDEX_WIDTH = 7;
    constexpr size_t NAME_WIDTH = 22;
    constexpr size_t TRUNCATE_LENGTH = 19;

    // Prepare header
    std::ostringstream header;
    header << "+" << std::setfill('-') << std::setw(INDEX_WIDTH + 2) << "-"
           << "+" << std::setw(NAME_WIDTH + 2) << "-" << "+";
    LOG_INFO(header.str());

    std::ostringstream title;
    title << "| " << std::left << std::setw(INDEX_WIDTH) << "Index"
          << " | " << std::left << std::setw(NAME_WIDTH) << "Device Name" << " |";
    LOG_INFO(title.str());

    LOG_INFO(header.str());

    for (UINT i = 0; i < deviceCount; ++i) {
        ComPtr<IMMDevice> device;
        hr = deviceCollection->Item(i, &device);
        if (FAILED(hr)) {
            LOG_WARNING("[WindowsManager::ListMonitorableDevices] Failed to get device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        if (FAILED(hr)) {
            LOG_WARNING("[WindowsManager::ListMonitorableDevices] Failed to open property store for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        // Wrapper to ensure PropVariant is cleared
        struct PropVariantWrapper {
            PROPVARIANT var;
            PropVariantWrapper() { PropVariantInit(&var); }
            ~PropVariantWrapper() { PropVariantClear(&var); }
            // Disable copy
            PropVariantWrapper(const PropVariantWrapper&) = delete;
            PropVariantWrapper& operator=(const PropVariantWrapper&) = delete;
            // Enable move
            PropVariantWrapper(PropVariantWrapper&& other) noexcept {
                var = other.var;
                other.var.pwszVal = nullptr;
            }
            PropVariantWrapper& operator=(PropVariantWrapper&& other) noexcept {
                if (this != &other) {
                    PropVariantClear(&var);
                    var = other.var;
                    other.var.pwszVal = nullptr;
                }
                return *this;
            }
        } varName;

        hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName.var);
        if (FAILED(hr) || varName.var.vt != VT_LPWSTR || varName.var.pwszVal == nullptr) {
            LOG_WARNING("[WindowsManager::ListMonitorableDevices] Device at index " + std::to_string(i) + " has invalid or missing friendly name.");
            continue;
        }

        std::wstring deviceNameW(varName.var.pwszVal);
        std::string deviceName = VolumeUtils::ConvertWStringToString(deviceNameW);

        if (deviceName.length() > NAME_WIDTH) {
            deviceName = deviceName.substr(0, TRUNCATE_LENGTH) + "...";
        }

        // Format index and device name using string streams
        std::ostringstream row;
        row << "| " << std::left << std::setw(INDEX_WIDTH) << i
            << " | " << std::left << std::setw(NAME_WIDTH) << deviceName << " |";
        LOG_INFO(row.str());
    }

    LOG_INFO(header.str());
}

bool WindowsManager::UnregisterVolumeChangeCallback(CallbackID callbackID) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    size_t erased = volumeChangeCallbacks_.erase(callbackID);
    LOG_DEBUG("[WindowsManager::UnregisterVolumeChangeCallback] Callback ID " + std::to_string(callbackID) + " erased: " + std::to_string(erased));
    return erased > 0;
}