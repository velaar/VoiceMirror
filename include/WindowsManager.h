#pragma once

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Project headers
#include "Defconf.h"
#include "Logger.h"
#include "SoundManager.h"
#include "VolumeUtils.h"

using Microsoft::WRL::ComPtr;
using CallbackID = unsigned int;

class WindowsManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
   public:
    WindowsManager(const Config& config);
    ~WindowsManager();
    // Add near the top with other using statements
    using DeviceCallback = std::function<void()>;
    std::function<void()> onDevicePluggedIn;
    std::function<void()> onDeviceUnplugged;
    // Add to private members
    DeviceCallback onDevicePluggedIn_;
    DeviceCallback onDeviceUnplugged_;
    void CheckDevice(LPCWSTR deviceId, bool isAdded);

    // Add to public members
    void SetDevicePluggedInCallback(DeviceCallback callback);
    void SetDeviceUnpluggedCallback(DeviceCallback callback);
    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    CallbackID RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    bool UnregisterVolumeChangeCallback(CallbackID id);
    void SetRestartAudioEngineCallback(std::function<void()> callback);
    void ListMonitorableDevices();
    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();

   private:
    Config config_;
    unsigned int hotkeyModifiers_;
    unsigned int hotkeyVK_;
    std::string syncSoundFilePath_;
    bool comInitialized_;
    HWND hwndHotkeyWindow_;

    ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    ComPtr<IMMDevice> speakers_;
    ComPtr<IAudioEndpointVolume> endpointVolume_;

    mutable std::mutex soundMutex_;
    mutable std::mutex callbackMutex_;
    mutable std::mutex comInitializedMutex_;
    std::atomic<ULONG> refCount_{1};

    std::array<std::function<void(float, bool)>, MAX_CALLBACKS> callbacks_{};
    std::array<std::optional<CallbackID>, MAX_CALLBACKS> callbackIDs_{};
    CallbackID nextCallbackID_ = 1;

    std::function<void()> restartAudioEngineCallback_;
    mutable std::mutex restartCallbackMutex_;

    bool InitializeCOM();
    void UninitializeCOM();
    bool InitializeCOMInterfaces();
    void Cleanup();
    void ReinitializeCOMInterfaces();

    bool InitializeHotkey();
    void CleanupHotkey();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void WindowProcCallback();

    void PlaySoundFromFile(const std::wstring& soundFilePath, uint16_t delayMs, bool playSync);

    std::string WideStringToUTF8(const std::wstring& wideStr);
    std::wstring UTF8ToWideString(const std::string& utf8Str);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG)
    AddRef() override;
    STDMETHODIMP_(ULONG)
    Release() override;

    float previousVolume = -1.0f;
    bool previousMute = false;
    
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
};
