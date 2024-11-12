#include "WindowsManager.h"

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>  // For PlaySoundW
#include <propvarutil.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"  // Assumed utility for volume conversions

// Constructor
WindowsManager::WindowsManager(const Config& config)
    : config_(config),
      hotkeyModifiers_(config.hotkeyModifiers.value),
      hotkeyVK_(config.hotkeyVK.value),
      syncSoundFilePath_(config.syncSoundFilePath.value ? WideStringToUTF8(config.syncSoundFilePath.value) : WideStringToUTF8(DEFAULT_SYNC_SOUND_FILE)),
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

// Set Volume
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

// Set Mute
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

// Get Volume
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


// Get Mute
bool WindowsManager::GetMute() const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }

    BOOL muted = FALSE;
    HRESULT hr = endpointVolume_->GetMute(&muted);
    return SUCCEEDED(hr) ? (muted != FALSE) : false;
}

// Register Callback
CallbackID WindowsManager::RegisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (size_t i = 0; i < MAX_CALLBACKS; ++i) {
        if (!callbackIDs_[i].has_value()) {
            CallbackID id = nextCallbackID_++;
            callbacks_[i] = std::move(callback);
            callbackIDs_[i] = id;

            LOG_DEBUG("[WindowsManager::RegisterVolumeChangeCallback] Registered callback ID: " + std::to_string(id));
            return id;
        }
    }
    LOG_ERROR("[WindowsManager::RegisterVolumeChangeCallback] Maximum number of callbacks reached.");
    throw std::runtime_error("Maximum number of callbacks reached.");
}

// Unregister Callback
bool WindowsManager::UnregisterVolumeChangeCallback(CallbackID id) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (size_t i = 0; i < MAX_CALLBACKS; ++i) {
        if (callbackIDs_[i].has_value() && callbackIDs_[i].value() == id) {
            callbacks_[i] = nullptr;
            callbackIDs_[i].reset();
            LOG_DEBUG("[WindowsManager::UnregisterVolumeChangeCallback] Unregistered callback ID: " + std::to_string(id));
            return true;
        }
    }
    LOG_WARNING("[WindowsManager::UnregisterVolumeChangeCallback] Failed to find callback ID: " + std::to_string(id));
    return false;
}

// Set Restart Audio Engine Callback
void WindowsManager::SetRestartAudioEngineCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(restartCallbackMutex_);
    restartAudioEngineCallback_ = std::move(callback);
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

    return true;
}

// Cleanup Hotkey
void WindowsManager::CleanupHotkey() {
    std::lock_guard<std::mutex> lock(soundMutex_);
    if (hwndHotkeyWindow_) {
        UnregisterHotKey(hwndHotkeyWindow_, 1);
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
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
    std::function<void()> callbackCopy;
    {
        std::lock_guard<std::mutex> lock(restartCallbackMutex_);
        callbackCopy = restartAudioEngineCallback_;
    }
    if (callbackCopy)
        callbackCopy();
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

// OnNotify Callback
STDMETHODIMP WindowsManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) {
        LOG_ERROR("[WindowsManager::OnNotify] Received null notification data.");
        return E_POINTER;
    }

    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);

    LOG_DEBUG("[WindowsManager::OnNotify] Notification received. Volume: " + std::to_string(newVolume) + "%, Mute: " + (newMute ? "Muted" : "Unmuted"));

    if (std::abs(newVolume - previousVolume) < 1.0f && newMute == previousMute) {
        LOG_DEBUG("[WindowsManager::OnNotify] Change is below threshold, skipping update.");
        return S_OK;
    }

    previousVolume = newVolume;
    previousMute = newMute;

    for (const auto& callback : callbacks_) {
        if (callback) {
            LOG_DEBUG("[WindowsManager::OnNotify] Executing registered volume change callback with Volume: " + std::to_string(newVolume) + "%, Mute: " + (newMute ? "Muted" : "Unmuted"));
            callback(newVolume, newMute);
        }
    }

    return S_OK;
}

// IMMNotificationClient Methods
STDMETHODIMP WindowsManager::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = WideStringToUTF8(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceStateChanged] Device ID: " + deviceId + ", New State: " + std::to_string(dwNewState) + ".");

    if (dwNewState == DEVICE_STATE_ACTIVE) {
        CheckDevice(pwstrDeviceId, true);
    } else if (dwNewState == DEVICE_STATE_DISABLED || dwNewState == DEVICE_STATE_UNPLUGGED) {
        CheckDevice(pwstrDeviceId, false);
    } else {
        LOG_DEBUG("[WindowsManager::OnDeviceStateChanged] Device state changed to an unhandled state.");
    }
    return S_OK;
}

// Handle Device Plugged In
void WindowsManager::HandleDevicePluggedIn() {
    LOG_DEBUG("[WindowsManager::HandleDevicePluggedIn] Handling device plugged in event.");
    if (onDevicePluggedIn) {
        onDevicePluggedIn();
        LOG_INFO("[WindowsManager::HandleDevicePluggedIn] Device plugged in event handled.");
    } else {
        LOG_WARNING("[WindowsManager::HandleDevicePluggedIn] onDevicePluggedIn callback is not set.");
    }
}

// Handle Device Unplugged
void WindowsManager::HandleDeviceUnplugged() {
    LOG_DEBUG("[WindowsManager::HandleDeviceUnplugged] Handling device unplugged event.");
    if (onDeviceUnplugged) {
        onDeviceUnplugged();
        LOG_INFO("[WindowsManager::HandleDeviceUnplugged] Device unplugged event handled.");
    } else {
        LOG_WARNING("[WindowsManager::HandleDeviceUnplugged] onDeviceUnplugged callback is not set.");
    }
}

STDMETHODIMP WindowsManager::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = WideStringToUTF8(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceAdded] Device added: " + deviceId + ".");
    CheckDevice(pwstrDeviceId, true);
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = WideStringToUTF8(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDeviceRemoved] Device removed: " + deviceId + ".");
    CheckDevice(pwstrDeviceId, false);
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    std::wstring wsDeviceId(pwstrDefaultDeviceId);
    std::string deviceId = WideStringToUTF8(wsDeviceId);
    LOG_INFO("[WindowsManager::OnDefaultDeviceChanged] Default device changed. Flow: " + std::to_string(flow) +
             ", Role: " + std::to_string(role) + ", Device ID: " + deviceId + ".");
    // Handle default device change if necessary
    return S_OK;
}

STDMETHODIMP WindowsManager::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
    std::wstring wsDeviceId(pwstrDeviceId);
    std::string deviceId = WideStringToUTF8(wsDeviceId);
    LOG_INFO("[WindowsManager::OnPropertyValueChanged] Device ID: " + deviceId + ", Property Key: {" +
             std::to_string(key.fmtid.Data1) + ", " + std::to_string(key.pid) + "}.");
    // Handle property value change if necessary
    return S_OK;
}

// Check Device
void WindowsManager::CheckDevice(LPCWSTR deviceId, bool isAdded) {
    std::wstring ws(deviceId);
    std::string deviceUUID = WideStringToUTF8(ws);
    if (deviceUUID == config_.monitorDeviceUUID.value) {
        if (isAdded)
            HandleDevicePluggedIn();
        else
            HandleDeviceUnplugged();
    }
}

// Play Sound
void WindowsManager::PlaySoundFromFile(const std::wstring& soundFilePath, uint16_t delayMs, bool playSync) {
    SoundManager::Instance().PlaySyncSound();
}

// String Conversion
std::string WindowsManager::WideStringToUTF8(const std::wstring& wideStr) {
    if (wideStr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "Unknown Device";
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), &utf8Str[0], sizeNeeded, nullptr, nullptr);
    return utf8Str;
}

std::wstring WindowsManager::UTF8ToWideString(const std::string& utf8Str) {
    if (utf8Str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return L"Unknown Device";
    std::wstring wideStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), &wideStr[0], sizeNeeded);
    return wideStr;
}

// List Monitorable Devices
void WindowsManager::ListMonitorableDevices() {
    std::lock_guard<std::mutex> lock(soundMutex_);

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(hr)) return;

    ComPtr<IMMDeviceCollection> deviceCollection;
    hr = enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr)) return;

    UINT deviceCount = 0;
    hr = deviceCollection->GetCount(&deviceCount);
    if (FAILED(hr)) return;

    const std::string separator = "+---------+----------------------+";
    LOG_INFO(separator);
    LOG_INFO("| Index   | Device Name           |");
    LOG_INFO(separator);

    for (UINT i = 0; i < deviceCount; ++i) {
        ComPtr<IMMDevice> device;
        hr = deviceCollection->Item(i, &device);
        if (FAILED(hr)) continue;

        ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        if (FAILED(hr)) continue;

        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
        if (FAILED(hr)) {
            PropVariantClear(&varName);
            continue;
        }

        std::wstring deviceNameW(varName.pwszVal);
        std::string deviceName = WideStringToUTF8(deviceNameW);
        PropVariantClear(&varName);

        if (deviceName.length() > 22)
            deviceName = deviceName.substr(0, 19) + "...";

        std::string indexStr = std::to_string(i);
        while (indexStr.length() < 7)
            indexStr = " " + indexStr;

        std::string deviceNameFormatted = deviceName;
        while (deviceNameFormatted.length() < 22)
            deviceNameFormatted += " ";

        LOG_INFO("| " + indexStr + " | " + deviceNameFormatted + " |");
    }

    LOG_INFO(separator);
}
void WindowsManager::SetDevicePluggedInCallback(std::function<void()> callback) {
    onDevicePluggedIn = std::move(callback);
}

void WindowsManager::SetDeviceUnpluggedCallback(std::function<void()> callback) {
    onDeviceUnplugged = std::move(callback);
}