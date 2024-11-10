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
#include <stdexcept>
#include <thread>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"  // Assumed utility for volume conversions

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Winmm.lib")  // For PlaySoundW

using Microsoft::WRL::ComPtr;

// Constructor
WindowsManager::WindowsManager(const Config& config)
    : config_(config),
      hotkeyModifiers_(config.hotkeyModifiers.value),
      hotkeyVK_(config.hotkeyVK.value),
      syncSoundFilePath_(config.syncSoundFilePath.value ? config.syncSoundFilePath.value : DEFAULT_SYNC_SOUND_FILE),
      comInitialized_(false) {
    LOG_DEBUG("[WindowsManager::WindowsManager] Constructor called with deviceUUID: " + std::string(config.monitorDeviceUUID.value));

    try {
        if (!InitializeCOM()) {
            throw std::runtime_error("COM initialization failed");
        }
        if (!InitializeCOMInterfaces()) {
            throw std::runtime_error("COM interfaces initialization failed");
        }
        // Register volume change notification
        HRESULT hr = endpointVolume_->RegisterControlChangeNotify(this);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::WindowsManager] Volume notification registration failed. HRESULT: " + std::to_string(hr));
            Cleanup();
            UninitializeCOM();
            throw std::runtime_error("Volume notification registration failed");
        }
        LOG_INFO("[WindowsManager::WindowsManager] COM interfaces initialized and volume notification registered successfully.");

        // Register device notification callback
        hr = deviceEnumerator_->RegisterEndpointNotificationCallback(this);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::WindowsManager] Device notification registration failed. HRESULT: " + std::to_string(hr));
            endpointVolume_->UnregisterControlChangeNotify(this);
            Cleanup();
            UninitializeCOM();
            throw std::runtime_error("Device notification registration failed");
        }
        LOG_INFO("[WindowsManager::WindowsManager] Device notification registered successfully.");

        // Initialize hotkey
        if (!InitializeHotkey()) {
            LOG_WARNING("[WindowsManager::WindowsManager] Hotkey initialization failed.");
            // Not throwing exception as hotkey might be optional
        }

        // Play startup sound if enabled
        if (config_.startupSound.value) {
            PlayStartupSound(
                UTF8ToWideString(config_.startupSoundFilePath.value ? config_.startupSoundFilePath.value : DEFAULT_STARTUP_SOUND_FILE),
                config_.startupSoundDelay.value);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[WindowsManager::WindowsManager] Exception caught: " + std::string(e.what()));
        Cleanup();
        UninitializeCOM();
        throw;
    }
}

// Destructor
WindowsManager::~WindowsManager() {
    LOG_DEBUG("[WindowsManager::~WindowsManager] Destructor called.");

    // Clean up hotkey
    CleanupHotkey();

    // Unregister volume notifications
    if (endpointVolume_) {
        HRESULT hr = endpointVolume_->UnregisterControlChangeNotify(this);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::~WindowsManager] Failed to unregister volume change notification. HRESULT: " + std::to_string(hr));
        } else {
            LOG_DEBUG("[WindowsManager::~WindowsManager] Volume change notification unregistered successfully.");
        }
    }

    // Unregister device notification callback
    if (deviceEnumerator_) {
        HRESULT hr = deviceEnumerator_->UnregisterEndpointNotificationCallback(this);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::~WindowsManager] Failed to unregister device notification callback. HRESULT: " + std::to_string(hr));
        } else {
            LOG_DEBUG("[WindowsManager::~WindowsManager] Device notification callback unregistered successfully.");
        }
    }

    // Clean up COM interfaces
    Cleanup();

    // Uninitialize COM
    UninitializeCOM();

    LOG_INFO("[WindowsManager::~WindowsManager] WindowsManager destroyed successfully.");
}

// COM Initialization
bool WindowsManager::InitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex_);
    LOG_DEBUG("[WindowsManager::InitializeCOM] Attempting to initialize COM.");

    if (!comInitialized_) {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
            LOG_INFO("[WindowsManager::InitializeCOM] COM initialized successfully.");
            return true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            comInitialized_ = true;  // Already initialized with different mode
            LOG_WARNING("[WindowsManager::InitializeCOM] COM already initialized with a different concurrency model. HRESULT: " + std::to_string(hr));
            return true;
        } else {
            LOG_ERROR("[WindowsManager::InitializeCOM] COM initialization failed. HRESULT: " + std::to_string(hr));
            return false;
        }
    }
    LOG_DEBUG("[WindowsManager::InitializeCOM] COM is already initialized.");
    return true;
}

void WindowsManager::UninitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex_);
    if (comInitialized_) {
        ::CoUninitialize();
        comInitialized_ = false;
        LOG_INFO("[WindowsManager::UninitializeCOM] COM uninitialized successfully.");
    } else {
        LOG_DEBUG("[WindowsManager::UninitializeCOM] COM was not initialized.");
    }
}

// Initialize COM Interfaces
bool WindowsManager::InitializeCOMInterfaces() {
    LOG_DEBUG("[WindowsManager::InitializeCOMInterfaces] Initializing COM interfaces.");

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(deviceEnumerator_.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to create IMMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return false;
    }

    hr = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &speakers_);
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to get default audio endpoint. HRESULT: " + std::to_string(hr));
        return false;
    }

    hr = speakers_->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(endpointVolume_.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::InitializeCOMInterfaces] Failed to activate IAudioEndpointVolume. HRESULT: " + std::to_string(hr));
        return false;
    }

    LOG_INFO("[WindowsManager::InitializeCOMInterfaces] COM interfaces initialized successfully.");
    return true;
}

// Cleanup COM Interfaces
void WindowsManager::Cleanup() {
    LOG_DEBUG("[WindowsManager::Cleanup] Cleaning up COM interfaces.");

    if (endpointVolume_) {
        endpointVolume_.Reset();
        LOG_DEBUG("[WindowsManager::Cleanup] IAudioEndpointVolume interface reset.");
    }
    if (speakers_) {
        speakers_.Reset();
        LOG_DEBUG("[WindowsManager::Cleanup] IMMDevice interface reset.");
    }
    if (deviceEnumerator_) {
        deviceEnumerator_.Reset();
        LOG_DEBUG("[WindowsManager::Cleanup] IMMDeviceEnumerator interface reset.");
    }
    LOG_INFO("[WindowsManager::Cleanup] COM interfaces cleaned up.");
}

// Reinitialize COM Interfaces
void WindowsManager::ReinitializeCOMInterfaces() {
    LOG_DEBUG("[WindowsManager::ReinitializeCOMInterfaces] Reinitializing COM interfaces.");

    std::lock_guard<std::mutex> lock(soundMutex_);

    Cleanup();

    if (!InitializeCOMInterfaces()) {
        LOG_ERROR("[WindowsManager::ReinitializeCOMInterfaces] COM interface reinitialization failed.");
        throw std::runtime_error("COM interface reinitialization failed");
    }

    // Re-register volume change notifications
    HRESULT hr = endpointVolume_->RegisterControlChangeNotify(this);
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::ReinitializeCOMInterfaces] Failed to re-register volume change notification. HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Failed to re-register volume change notification. HRESULT: " + std::to_string(hr));
    }

    LOG_INFO("[WindowsManager::ReinitializeCOMInterfaces] COM interfaces reinitialized and volume notification re-registered successfully.");
}

// Set Volume
bool WindowsManager::SetVolume(float volumePercent) {
    LOG_DEBUG("[WindowsManager::SetVolume] Setting volume to " + std::to_string(volumePercent) + "%.");

    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        LOG_WARNING("[WindowsManager::SetVolume] Invalid volumePercent: " + std::to_string(volumePercent) + "%. Must be between 0 and 100.");
        return false;
    }

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::SetVolume] EndpointVolume is not initialized. Attempting to reinitialize.");
        try {
            ReinitializeCOMInterfaces();
        } catch (const std::exception& e) {
            LOG_ERROR("[WindowsManager::SetVolume] Reinitialization failed: " + std::string(e.what()));
            return false;
        }
    }

    float scalar = VolumeUtils::PercentToScalar(volumePercent);
    HRESULT hr = endpointVolume_->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (SUCCEEDED(hr)) {
        LOG_INFO("[WindowsManager::SetVolume] Volume set to " + std::to_string(volumePercent) + "% (" + std::to_string(scalar) + " scalar).");
        return true;
    } else {
        LOG_ERROR("[WindowsManager::SetVolume] Failed to set volume. HRESULT: " + std::to_string(hr));
        return false;
    }
}

// Set Mute
bool WindowsManager::SetMute(bool mute) {
    LOG_DEBUG("[WindowsManager::SetMute] Setting mute state to " + std::string(mute ? "Muted" : "Unmuted") + ".");

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::SetMute] EndpointVolume is not initialized. Attempting to reinitialize.");
        try {
            ReinitializeCOMInterfaces();
        } catch (const std::exception& e) {
            LOG_ERROR("[WindowsManager::SetMute] Reinitialization failed: " + std::string(e.what()));
            return false;
        }
    }

    HRESULT hr = endpointVolume_->SetMute(mute, nullptr);
    if (SUCCEEDED(hr)) {
        LOG_INFO("[WindowsManager::SetMute] Mute " + std::string(mute ? "enabled" : "disabled") + " successfully.");
        return true;
    } else {
        LOG_ERROR("[WindowsManager::SetMute] Failed to set mute. HRESULT: " + std::to_string(hr));
        return false;
    }
}

// Get Volume
float WindowsManager::GetVolume() const {
    LOG_DEBUG("[WindowsManager::GetVolume] Retrieving current volume.");

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::GetVolume] EndpointVolume is not initialized. Attempting to reinitialize.");
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }

    float currentVolume = 0.0f;
    HRESULT hr = endpointVolume_->GetMasterVolumeLevelScalar(&currentVolume);
    if (SUCCEEDED(hr)) {
        float volumePercent = VolumeUtils::ScalarToPercent(currentVolume);
        LOG_INFO("[WindowsManager::GetVolume] Current volume: " + std::to_string(volumePercent) + "% (" + std::to_string(currentVolume) + " scalar).");
        return volumePercent;
    }
    LOG_ERROR("[WindowsManager::GetVolume] Failed to get volume. HRESULT: " + std::to_string(hr));
    return -1.0f;  // Indicates error
}

// Get Mute
bool WindowsManager::GetMute() const {
    LOG_DEBUG("[WindowsManager::GetMute] Retrieving current mute state.");

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!endpointVolume_) {
        LOG_WARNING("[WindowsManager::GetMute] EndpointVolume is not initialized. Attempting to reinitialize.");
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }

    BOOL muted = FALSE;
    HRESULT hr = endpointVolume_->GetMute(&muted);
    if (SUCCEEDED(hr)) {
        LOG_INFO("[WindowsManager::GetMute] Current mute state: " + std::string(muted ? "Muted" : "Unmuted") + ".");
        return muted != FALSE;
    }
    LOG_ERROR("[WindowsManager::GetMute] Failed to get mute status. HRESULT: " + std::to_string(hr));
    return false;  // Default to not muted on error
}

// Register Volume Change Callback
void WindowsManager::RegisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacks_.emplace_back(std::move(callback));
    }
    LOG_INFO("[WindowsManager::RegisterVolumeChangeCallback] Volume change callback registered.");
}

// Unregister Volume Change Callback
void WindowsManager::UnregisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    size_t beforeErase = callbacks_.size();
    callbacks_.erase(
        std::remove_if(callbacks_.begin(), callbacks_.end(),
                       [&callback](const auto& cb) {
                           // Comparing target types; more sophisticated comparison can be implemented if needed
                           return cb.target_type() == callback.target_type();
                       }),
        callbacks_.end());
    size_t afterErase = callbacks_.size();
    LOG_INFO("[WindowsManager::UnregisterVolumeChangeCallback] Volume change callback unregistered. Callbacks before: " + std::to_string(beforeErase) + ", after: " + std::to_string(afterErase) + ".");
}

// Set Restart Audio Engine Callback
void WindowsManager::SetRestartAudioEngineCallback(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        restartAudioEngineCallback_ = std::move(callback);
    }
    LOG_INFO("[WindowsManager::SetRestartAudioEngineCallback] Restart audio engine callback set.");
}

// Set Hotkey Settings
void WindowsManager::SetHotkeySettings(uint16_t modifiers, uint8_t vk) {
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        hotkeyModifiers_ = modifiers;
        hotkeyVK_ = vk;
    }
    LOG_INFO("[WindowsManager::SetHotkeySettings] Hotkey settings updated. Modifiers: " + std::to_string(modifiers) + ", VK: " + std::to_string(vk) + ".");
    CleanupHotkey();
    InitializeHotkey();
}

// Initialize Hotkey
bool WindowsManager::InitializeHotkey() {
    LOG_DEBUG("[WindowsManager::InitializeHotkey] Initializing hotkey.");

    std::lock_guard<std::mutex> lock(soundMutex_);

    // Register a window class for the hidden window
    const wchar_t CLASS_NAME[] = L"VoiceMirrorHotkeyHiddenWindow";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowsManager::WindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {  // Ignore if already registered
            LOG_ERROR("[WindowsManager::InitializeHotkey] Failed to register hidden window class. Error code: " + std::to_string(error));
            return false;
        } else {
            LOG_WARNING("[WindowsManager::InitializeHotkey] Hidden window class already registered. Error code: " + std::to_string(error));
        }
    } else {
        LOG_DEBUG("[WindowsManager::InitializeHotkey] Hidden window class registered successfully.");
    }

    // Create a hidden window
    hwndHotkeyWindow_ = CreateWindowExW(
        0,                        // Optional window styles
        CLASS_NAME,               // Window class
        L"Hotkey Hidden Window",  // Window name
        0,                        // Window style
        0, 0, 0, 0,               // Position and size
        HWND_MESSAGE,             // Parent window (message-only)
        NULL,                     // Menu
        wc.hInstance,             // Instance handle
        this                      // Additional application data
    );

    if (!hwndHotkeyWindow_) {
        DWORD error = GetLastError();
        LOG_ERROR("[WindowsManager::InitializeHotkey] Failed to create hidden window. Error code: " + std::to_string(error));
        return false;
    } else {
        LOG_DEBUG("[WindowsManager::InitializeHotkey] Hidden window created successfully.");
    }

    // Associate the window with this instance
    if (SetWindowLongPtrW(hwndHotkeyWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)) == 0) {
        DWORD error = GetLastError();
        LOG_ERROR("[WindowsManager::InitializeHotkey] Failed to associate hidden window with WindowsManager instance. Error code: " + std::to_string(error));
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
        return false;
    } else {
        LOG_DEBUG("[WindowsManager::InitializeHotkey] Associated hidden window with WindowsManager instance.");
    }

    // Register the hotkey
    if (!RegisterHotKey(hwndHotkeyWindow_, 1, hotkeyModifiers_, hotkeyVK_)) {
        DWORD error = GetLastError();
        LOG_ERROR("[WindowsManager::InitializeHotkey] Failed to register hotkey. Error code: " + std::to_string(error));
        DestroyWindow(hwndHotkeyWindow_);
        hwndHotkeyWindow_ = nullptr;
        return false;
    } else {
        LOG_INFO("[WindowsManager::InitializeHotkey] Hotkey registered successfully with modifiers: " + std::to_string(hotkeyModifiers_) + ", VK: " + std::to_string(hotkeyVK_) + ".");
    }

    return true;
}

// Cleanup Hotkey
void WindowsManager::CleanupHotkey() {
    LOG_DEBUG("[WindowsManager::CleanupHotkey] Cleaning up hotkey.");

    std::lock_guard<std::mutex> lock(soundMutex_);

    // Unregister the hotkey
    if (hwndHotkeyWindow_) {
        if (UnregisterHotKey(hwndHotkeyWindow_, 1)) {
            LOG_INFO("[WindowsManager::CleanupHotkey] Hotkey unregistered successfully.");
        } else {
            DWORD error = GetLastError();
            LOG_ERROR("[WindowsManager::CleanupHotkey] Failed to unregister hotkey. Error code: " + std::to_string(error));
        }

        // Destroy the hidden window
        DestroyWindow(hwndHotkeyWindow_);
        LOG_DEBUG("[WindowsManager::CleanupHotkey] Hidden window destroyed.");
        hwndHotkeyWindow_ = nullptr;
    }
}

// Window Procedure to handle hotkey
LRESULT CALLBACK WindowsManager::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_HOTKEY) {
        // Retrieve the WindowsManager instance associated with this window
        WindowsManager* pThis = reinterpret_cast<WindowsManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (pThis) {
            if (pThis->restartAudioEngineCallback_) {
                LOG_INFO("[WindowsManager::WindowProc] Hotkey pressed: Executing restart audio engine callback.");
                pThis->restartAudioEngineCallback_();
            } else {
                LOG_WARNING("[WindowsManager::WindowProc] Hotkey pressed but restart callback is not set.");
            }
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// IUnknown Methods
STDMETHODIMP WindowsManager::QueryInterface(REFIID riid, void** ppvInterface) {
    if (!ppvInterface) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppvInterface = static_cast<IAudioEndpointVolumeCallback*>(this);
    } else if (riid == __uuidof(IMMNotificationClient)) {
        *ppvInterface = static_cast<IMMNotificationClient*>(this);
    } else {
        *ppvInterface = nullptr;
        LOG_WARNING("[WindowsManager::QueryInterface] Interface not supported.");
        return E_NOINTERFACE;
    }
    AddRef();
    LOG_DEBUG("[WindowsManager::QueryInterface] Interface requested and provided.");
    return S_OK;
}

STDMETHODIMP_(ULONG)
WindowsManager::AddRef() {
    ULONG ulRef = refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    LOG_DEBUG("[WindowsManager::AddRef] Reference count increased to " + std::to_string(ulRef) + ".");
    return ulRef;
}

STDMETHODIMP_(ULONG)
WindowsManager::Release() {
    ULONG ulRef = refCount_.fetch_sub(1, std::memory_order_relaxed) - 1;
    LOG_DEBUG("[WindowsManager::Release] Reference count decreased to " + std::to_string(ulRef) + ".");
    if (ulRef == 0) {
        LOG_DEBUG("[WindowsManager::Release] Reference count is zero. Deleting instance.");
        delete this;
    }
    return ulRef;
}

// IAudioEndpointVolumeCallback Method
STDMETHODIMP WindowsManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) {
        LOG_ERROR("[WindowsManager::OnNotify] PAUDIO_VOLUME_NOTIFICATION_DATA is null.");
        return E_POINTER;
    }

    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);
    LOG_INFO("[WindowsManager::OnNotify] Volume changed: " + std::to_string(newVolume) + "%, Mute: " + (newMute ? "Yes" : "No") + ".");

    // Execute registered callbacks
    std::vector<std::function<void(float, bool)>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacksCopy = callbacks_;
    }
    for (const auto& callback : callbacksCopy) {
        if (callback) callback(newVolume, newMute);
    }
    LOG_DEBUG("[WindowsManager::OnNotify] Volume change callbacks executed.");

    // Optionally play sync sound
    if (config_.chime.value) {
        PlaySyncSound();
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
    LOG_DEBUG("[WindowsManager::CheckDevice] Device " + deviceUUID + (isAdded ? " added." : " removed."));

    if (deviceUUID == config_.monitorDeviceUUID.value) {
        if (isAdded) {
            HandleDevicePluggedIn();
        } else {
            HandleDeviceUnplugged();
        }
    } else {
        LOG_DEBUG("[WindowsManager::CheckDevice] Device UUID does not match targetDeviceUUID.");
    }
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

// Play Startup Sound
void WindowsManager::PlayStartupSound(const std::wstring& soundFilePath, uint16_t delayMs) {
    LOG_DEBUG("[WindowsManager::PlayStartupSound] Preparing to play startup sound.");

    std::wstring wideFilePath = soundFilePath.empty() 
                                ? L"" 
                                : soundFilePath; // Default to empty if soundFilePath isn't provided

    // Spawn a thread to play the sound asynchronously after a delay
    std::thread([wideFilePath, delayMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        if (!wideFilePath.empty() && !PlaySoundW(wideFilePath.c_str(), NULL, SND_FILENAME | SND_ASYNC)) {
            LOG_ERROR("[WindowsManager::PlayStartupSound] Failed to play startup sound. Error code: " + std::to_string(GetLastError()));
        } else {
            LOG_INFO("[WindowsManager::PlayStartupSound] Startup sound played successfully.");
        }
    }).detach();
}


// Play Sync Sound
void WindowsManager::PlaySyncSound() {
    LOG_DEBUG("[WindowsManager::PlaySyncSound] Playing sync sound.");

    std::lock_guard<std::mutex> lock(soundMutex_);
    if (!syncSoundFilePath_.empty()) {
        if (!PlaySoundW(syncSoundFilePath_.c_str(), NULL, SND_FILENAME | SND_ASYNC)) {
            LOG_ERROR("[WindowsManager::PlaySyncSound] Failed to play sync sound: " + std::to_string(GetLastError()));
        } else {
            LOG_INFO("[WindowsManager::PlaySyncSound] Sync sound played successfully.");
        }
    } else {
        LOG_WARNING("[WindowsManager::PlaySyncSound] Sync sound file path is empty.");
    }
}

// WideString to UTF8 Conversion
std::string WindowsManager::WideStringToUTF8(const std::wstring& wideStr) {
    if (wideStr.empty()) {
        LOG_WARNING("[WindowsManager::WideStringToUTF8] Empty wide string provided.");
        return "";
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) {
        LOG_ERROR("[WindowsManager::WideStringToUTF8] WideCharToMultiByte failed. Returning 'Unknown Device'.");
        return "Unknown Device";
    }
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), &utf8Str[0], sizeNeeded, nullptr, nullptr);
    LOG_DEBUG("[WindowsManager::WideStringToUTF8] Converted wide string to UTF-8: " + utf8Str);
    return utf8Str;
}

std::wstring WindowsManager::UTF8ToWideString(const std::string& utf8Str) {
    if (utf8Str.empty()) {
        LOG_WARNING("[WindowsManager::UTF8ToWideString] Empty UTF-8 string provided.");
        return L"";
    }
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) {
        LOG_ERROR("[WindowsManager::UTF8ToWideString] MultiByteToWideChar failed.");
        return L"Unknown Device";
    }
    std::wstring wideStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), static_cast<int>(utf8Str.size()), &wideStr[0], sizeNeeded);
    LOG_DEBUG("[WindowsManager::UTF8ToWideString] Converted UTF-8 string to wide string.");
    return wideStr;
}

// List Monitorable Devices
void WindowsManager::ListMonitorableDevices() {
    LOG_DEBUG("[WindowsManager::ListMonitorableDevices] Listing monitorable devices.");

    std::lock_guard<std::mutex> lock(soundMutex_);

    LOG_INFO("[WindowsManager::ListMonitorableDevices] Listing Monitorable Devices:");

    HRESULT hr = S_OK;

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to create IMMDeviceEnumerator. HRESULT: " + std::to_string(hr));
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

    const std::string separator = "+---------+----------------------+";
    LOG_INFO("[WindowsManager::ListMonitorableDevices] " + separator);
    LOG_INFO("[WindowsManager::ListMonitorableDevices] | Index   | Device Name           |");
    LOG_INFO("[WindowsManager::ListMonitorableDevices] " + separator);

    for (UINT i = 0; i < deviceCount; ++i) {
        ComPtr<IMMDevice> device;
        hr = deviceCollection->Item(i, &device);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to get device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to open property store for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
        if (FAILED(hr)) {
            LOG_ERROR("[WindowsManager::ListMonitorableDevices] Failed to get device name for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            PropVariantClear(&varName);
            continue;
        }

        std::wstring deviceNameW(varName.pwszVal);
        std::string deviceName = WideStringToUTF8(deviceNameW);
        PropVariantClear(&varName);

        if (deviceName.length() > 22) {
            deviceName = deviceName.substr(0, 19) + "...";
            LOG_DEBUG("[WindowsManager::ListMonitorableDevices] Device name truncated to: " + deviceName);
        }

        std::string indexStr = std::to_string(i);
        while (indexStr.length() < 7) {
            indexStr = " " + indexStr;
        }

        std::string deviceNameFormatted = deviceName;
        while (deviceNameFormatted.length() < 22) {
            deviceNameFormatted += " ";
        }

        LOG_INFO("[WindowsManager::ListMonitorableDevices] | " + indexStr + " | " + deviceNameFormatted + " |");
    }

    LOG_INFO("[WindowsManager::ListMonitorableDevices] " + separator);
}
