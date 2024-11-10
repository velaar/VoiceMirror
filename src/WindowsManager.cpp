#include "WindowsManager.h"

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>
#include <propvarutil.h>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"


#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

WindowsManager::WindowsManager(const std::string& deviceUUID)
    : refCount(1),
      comInitMutex(CreateMutexA(NULL, FALSE, COM_INIT_MUTEX_NAME)),
      comInitialized(false),
      targetDeviceUUID(deviceUUID) {
    if (!comInitMutex.get()) {
        throw std::runtime_error("Failed to create COM init mutex");
    }
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
    } catch (const std::exception& e) {
        LOG_DEBUG(std::string("Exception caught: ") + e.what());
        Cleanup();
        throw;
    }
}


bool WindowsManager::InitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex);
    if (!comInitialized) {
        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized = true;
            return true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void WindowsManager::UninitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitializedMutex);
    if (comInitialized) {
        ::CoUninitialize();
        comInitialized = false;
    }
}

bool WindowsManager::InitializeCOMInterfaces() {
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(deviceEnumerator.GetAddressOf()));
    if (FAILED(hr)) return false;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr)) return false;
    hr = speakers->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_INPROC_SERVER,
        nullptr,
        reinterpret_cast<void**>(endpointVolume.GetAddressOf()));
    return SUCCEEDED(hr);
}

void WindowsManager::Cleanup() {
    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
        endpointVolume.Reset();
    }
    speakers.Reset();
    deviceEnumerator.Reset();
}

void WindowsManager::ReinitializeCOMInterfaces() {
    std::lock_guard<std::mutex> lock(soundMutex);
    Cleanup();
    if (!InitializeCOMInterfaces()) {
        throw std::runtime_error("COM interface reinitialization failed");
    }
}

bool WindowsManager::SetVolume(float volumePercent) {
    std::lock_guard<std::mutex> lock(soundMutex);
    if (!endpointVolume) {
        ReinitializeCOMInterfaces();
    }
    if (volumePercent < 0.0f || volumePercent > 100.0f) return false;
    float scalar = VolumeUtils::PercentToScalar(volumePercent);
    return SUCCEEDED(endpointVolume->SetMasterVolumeLevelScalar(scalar, nullptr));
}

bool WindowsManager::SetMute(bool mute) {
    std::lock_guard<std::mutex> lock(soundMutex);
    if (!endpointVolume) {
        ReinitializeCOMInterfaces();
    }
    return SUCCEEDED(endpointVolume->SetMute(mute, nullptr));
}

float WindowsManager::GetVolume() const {
    std::lock_guard<std::mutex> lock(soundMutex);
    if (!endpointVolume) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }
    float currentVolume = 0.0f;
    if (SUCCEEDED(endpointVolume->GetMasterVolumeLevelScalar(&currentVolume))) {
        return VolumeUtils::ScalarToPercent(currentVolume);
    }
    return -1.0f;
}

bool WindowsManager::GetMute() const {
    std::lock_guard<std::mutex> lock(soundMutex);
    if (!endpointVolume) {
        const_cast<WindowsManager*>(this)->ReinitializeCOMInterfaces();
    }
    BOOL muted = FALSE;
    if (SUCCEEDED(endpointVolume->GetMute(&muted))) {
        return muted != FALSE;
    }
    return false;
}


// Initialize hotkey registration
bool WindowsManager::InitializeHotkey() {
    // Define and register a window class for the message-only window
    const wchar_t CLASS_NAME[] = L"VoiceMirrorHotkeyWindowClass";

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowsManager::WindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) { // Ignore if already registered
            LOG_ERROR("Failed to register hotkey window class. Error code: " + std::to_string(error));
            return false;
        }
    }

    // Create a message-only window
    hwndHotkeyWindow = CreateWindowExW(
        0,                  // Optional window styles
        CLASS_NAME,         // Window class
        L"Hotkey Window",   // Window name
        0,                  // Window style
        0, 0, 0, 0,          // Position and size
        HWND_MESSAGE,       // Parent window (message-only)
        NULL,               // Menu
        wc.hInstance,       // Instance handle
        this                // Additional application data
    );

    if (!hwndHotkeyWindow) {
        LOG_ERROR("Failed to create hotkey window. Error code: " + std::to_string(GetLastError()));
        return false;
    }

    // Associate the window with this instance
    SetWindowLongPtrW(hwndHotkeyWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Register the hotkey
    if (!RegisterHotKey(hwndHotkeyWindow, 1, hotkeyModifiers, hotkeyVK)) {
        LOG_ERROR("Failed to register hotkey. Error code: " + std::to_string(GetLastError()));
        DestroyWindow(hwndHotkeyWindow);
        hwndHotkeyWindow = nullptr;
        return false;
    }

    LOG_INFO("Hotkey registered successfully.");

    // Start the message loop thread
    hotkeyRunning = true;
    hotkeyThread = std::thread([this]() {
        MSG msg;
        while (hotkeyRunning.load()) {
            // Wait for messages
            if (GetMessageW(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            } else {
                // WM_QUIT received
                break;
            }
        }
    });

    return true;
}

// Cleanup hotkey registration and message loop
void WindowsManager::CleanupHotkey() {
    // Unregister the hotkey
    if (hwndHotkeyWindow) {
        UnregisterHotKey(hwndHotkeyWindow, 1);
        DestroyWindow(hwndHotkeyWindow);
        hwndHotkeyWindow = nullptr;
    }

    // Stop the message loop thread
    if (hotkeyRunning.load()) {
        hotkeyRunning = false;
        PostMessageW(hwndHotkeyWindow, WM_QUIT, 0, 0);
        if (hotkeyThread.joinable()) {
            hotkeyThread.join();
        }
    }
}

// Window procedure to handle hotkey
LRESULT CALLBACK WindowsManager::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_HOTKEY) {
        // Retrieve the WindowsManager instance associated with this window
        WindowsManager* pThis = reinterpret_cast<WindowsManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (pThis && pThis->restartAudioEngineCallback) {
            pThis->restartAudioEngineCallback();
            LOG_INFO("Hotkey pressed: Restarting audio engine.");
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

WindowsManager::~WindowsManager() {

    CleanupHotkey();

    if (endpointVolume) {
        endpointVolume->UnregisterControlChangeNotify(this);
    }
    Cleanup();
    UninitializeCOM();

}


void WindowsManager::RegisterVolumeChangeCallback(std::function<void(float, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbacks.emplace_back(std::move(callback));
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

STDMETHODIMP WindowsManager::QueryInterface(REFIID riid, void** ppvInterface) {
    if (!ppvInterface) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback) || riid == __uuidof(IMMNotificationClient)) {
        AddRef();
        *ppvInterface = static_cast<IAudioEndpointVolumeCallback*>(this);
        return S_OK;
    }
    *ppvInterface = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) WindowsManager::AddRef() {
    return refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG) WindowsManager::Release() {
    ULONG ulRef = refCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (ulRef == 0) delete this;
    return ulRef;
}

STDMETHODIMP WindowsManager::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (!pNotify) return E_POINTER;
    float newVolume = VolumeUtils::ScalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);
    std::vector<std::function<void(float, bool)>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbacksCopy = callbacks;
    }
    for (const auto& callback : callbacksCopy) {
        if (callback) callback(newVolume, newMute);
    }
    return S_OK;
}

STDMETHODIMP WindowsManager::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    if (dwNewState == DEVICE_STATE_ACTIVE) {
        CheckDevice(pwstrDeviceId, true);
    } else if (dwNewState == DEVICE_STATE_DISABLED) {
        CheckDevice(pwstrDeviceId, false);
    }
    return S_OK;
}

void WindowsManager::CheckDevice(LPCWSTR deviceId, bool isAdded) {
    std::wstring ws(deviceId);
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.length()), NULL, 0, NULL, NULL);
    std::string deviceUUID(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.length()), &deviceUUID[0], size_needed, NULL, NULL);
    if (deviceUUID == targetDeviceUUID) {
        if (isAdded) HandleDevicePluggedIn();
        else HandleDeviceUnplugged();
    }
}

void WindowsManager::HandleDevicePluggedIn() {
    if (onDevicePluggedIn) {
        onDevicePluggedIn();
    }
}

void WindowsManager::HandleDeviceUnplugged() {
    if (onDeviceUnplugged) {
        onDeviceUnplugged();
    }
}

STDMETHODIMP WindowsManager::OnDeviceAdded(LPCWSTR) { return S_OK; }
STDMETHODIMP WindowsManager::OnDeviceRemoved(LPCWSTR) { return S_OK; }
STDMETHODIMP WindowsManager::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) { return S_OK; }
STDMETHODIMP WindowsManager::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) { return S_OK; }

void WindowsManager::PlayStartupSound() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // \Waiting for default voicemeeter startup delay
    const std::wstring soundFilePath = L"m95.mp3";
    constexpr const wchar_t* aliasName = L"StartupSound";
    std::wstring openCommand = L"open \"" + soundFilePath + L"\" type mpegvideo alias " + aliasName;
    mciSendStringW(openCommand.c_str(), NULL, 0, NULL);
    std::wstring playCommand = std::wstring(L"play ") + aliasName + L" wait";
    mciSendStringW(playCommand.c_str(), NULL, 0, NULL);
    std::wstring closeCommand = std::wstring(L"close ") + aliasName;
    mciSendStringW(closeCommand.c_str(), NULL, 0, NULL);
}

void WindowsManager::PlaySyncSound() {
    std::lock_guard<std::mutex> lock(soundMutex);
    PlaySoundW(SYNC_SOUND_FILE_PATH, NULL, SND_FILENAME | SND_ASYNC);
}

static std::string WideStringToUTF8(const std::wstring& wideStr) {
    if (wideStr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "Unknown Device";
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), &utf8Str[0], sizeNeeded, nullptr, nullptr);
    return utf8Str;
}

void WindowsManager::ListMonitorableDevices() {
    std::lock_guard<std::mutex> lock(soundMutex);

    LOG_INFO("Listing Monitorable Devices:");

    HRESULT hr = S_OK;

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create IMMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return;
    }

    ComPtr<IMMDeviceCollection> deviceCollection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to enumerate audio endpoints. HRESULT: " + std::to_string(hr));
        return;
    }

    UINT deviceCount = 0;
    hr = deviceCollection->GetCount(&deviceCount);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get device count. HRESULT: " + std::to_string(hr));
        return;
    }

    const std::string separator = "+---------+----------------------+";
    LOG_INFO(separator);
    LOG_INFO("| Index   | Device Name           |");
    LOG_INFO(separator);

    for (UINT i = 0; i < deviceCount; ++i) {
        ComPtr<IMMDevice> device;
        hr = deviceCollection->Item(i, &device);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to open property store for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get device name for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            PropVariantClear(&varName);
            continue;
        }

        std::wstring deviceNameW(varName.pwszVal);
        std::string deviceName = WideStringToUTF8(deviceNameW);
        PropVariantClear(&varName);

        if (deviceName.length() > 22) {
            deviceName = deviceName.substr(0, 19) + "...";
        }

        std::string indexStr = std::to_string(i);
        while (indexStr.length() < 7) {
            indexStr = " " + indexStr;
        }

        std::string deviceNameFormatted = deviceName;
        while (deviceNameFormatted.length() < 22) {
            deviceNameFormatted += " ";
        }

        LOG_INFO("| " + indexStr + " | " + deviceNameFormatted + " |");
    }

    LOG_INFO(separator);
}

std::string WindowsManager::WideStringToUTF8(const std::wstring& wideStr) {
    if (wideStr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return "Unknown Device";
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), static_cast<int>(wideStr.length()), &utf8Str[0], sizeNeeded, nullptr, nullptr);
    return utf8Str;
}