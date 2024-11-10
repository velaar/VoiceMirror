#ifndef WINDOWSMANAGER_H
#define WINDOWSMANAGER_H

#pragma once

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Defconf.h"

class WindowsManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
   public:
    // Updated constructor to accept Config
    explicit WindowsManager(const Config& config);
    ~WindowsManager();

    // Volume control methods
    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    // Callback registration
    void RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    void UnregisterVolumeChangeCallback(std::function<void(float, bool)> callback);

    // Device monitoring
    void CheckDevice(LPCWSTR deviceId, bool isAdded);

    // Hotkey and restart callbacks
    void SetRestartAudioEngineCallback(std::function<void()> callback);
    void SetHotkeySettings(uint16_t modifiers, uint8_t vk);

    // Utility methods
    void ListMonitorableDevices();

    // Methods for playing sounds
    void PlayStartupSound(const std::wstring& soundFilePath, uint16_t delayMs);
    void PlaySyncSound();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG)
    AddRef() override;
    STDMETHODIMP_(ULONG)
    Release() override;

    // IAudioEndpointVolumeCallback method
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

    // IMMNotificationClient methods
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;

    // Callbacks for device plug/unplug events
    std::function<void()> onDevicePluggedIn;
    std::function<void()> onDeviceUnplugged;

   private:
    // COM Initialization
    bool InitializeCOM();
    void UninitializeCOM();
    bool InitializeCOMInterfaces();
    void Cleanup();
    void ReinitializeCOMInterfaces();

    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();

    std::wstring syncSoundFilePath_;

    // Hotkey related
    bool InitializeHotkey();
    void CleanupHotkey();
    HWND hwndHotkeyWindow_;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Utility
    static std::string WideStringToUTF8(const std::wstring& wideStr);

    // Member variables
    std::atomic<ULONG> refCount_{1};
    std::vector<std::function<void(float, bool)>> callbacks_;
    mutable std::mutex callbackMutex_;  // Protects callbacks vector

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> speakers_;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume_;

    // Synchronization
    mutable std::mutex comInitializedMutex_;  // Protects comInitialized flag
    mutable std::mutex soundMutex_;           // Protects sound-related operations

    // COM Initialization
    bool comInitialized_;
    std::string targetDeviceUUID_;

    // Hotkey settings
    uint16_t hotkeyModifiers_;
    uint8_t hotkeyVK_;
    std::function<void()> restartAudioEngineCallback_;

    // Configuration
    const Config& config_;
    static std::wstring UTF8ToWideString(const std::string& utf8Str);
};

#endif  // WINDOWSMANAGER_H
