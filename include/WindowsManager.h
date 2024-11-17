// WindowsManager.h
#pragma once

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "Defconf.h"
#include "Logger.h"
#include "VolumeUtils.h"

using CallbackID = unsigned int;

class WindowsManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    // Constructor and Destructor
    WindowsManager(const Config& config);
    ~WindowsManager();

    // Volume Control Methods
    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    // Callback Registration
    CallbackID RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    bool UnregisterVolumeChangeCallback(CallbackID callbackID);

    // Device Enumeration
    void ListMonitorableDevices();  // Newly added method

    // IUnknown Methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

    // IMMNotificationClient Methods
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;

    // Device Event Callbacks
    std::function<void()> onDevicePluggedIn;
    std::function<void()> onDeviceUnplugged;

private:
    // COM Initialization and Interfaces
    bool InitializeCOM();
    void UninitializeCOM();
    bool InitializeCOMInterfaces();
    void Cleanup();
    void ReinitializeCOMInterfaces();

    // COM Interfaces
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> speakers_;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume_;

    // Reference Counting for COM
    std::atomic<ULONG> refCount_{1};

    // Configuration and State
    Config config_;
    float previousVolume_ = -1.0f;
    bool previousMute_ = false;

    // Mutex for Sound Operations
    mutable std::mutex soundMutex_;

    // Hotkey Handling Members
    bool InitializeHotkey();
    void CleanupHotkey();
    void WindowProcCallback();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND hwndHotkeyWindow_;
    uint16_t hotkeyModifiers_;
    uint8_t hotkeyVK_;

    // COM Initialization State
    bool comInitialized_;
    std::mutex comInitializedMutex_;

    // Callback Management
    std::mutex callbackMutex_;
    std::map<CallbackID, std::function<void(float, bool)>> volumeChangeCallbacks_;
    CallbackID nextCallbackID_ = 1;

    // Constants for Device Enumeration Formatting
    static constexpr size_t INDEX_WIDTH = 7;
    static constexpr size_t NAME_WIDTH = 22;
    static constexpr size_t TRUNCATE_LENGTH = 19;
};
