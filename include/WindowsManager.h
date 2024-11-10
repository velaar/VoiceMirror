#ifndef WINDOWSMANAGER_H
#define WINDOWSMANAGER_H

#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include "RAIIHandle.h"

class WindowsManager : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    WindowsManager(const std::string& deviceUUID);
    ~WindowsManager();

    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    void RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    void UnregisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    void CheckDevice(LPCWSTR deviceId, bool isAdded);

    void SetRestartAudioEngineCallback(std::function<void()> callback);
    void ListMonitorableDevices();

    // Public methods for playing sounds
    void PlayStartupSound();
    void PlaySyncSound();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback methods
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
    bool InitializeCOM();
    void UninitializeCOM();
    bool InitializeCOMInterfaces();
    void Cleanup();
    void ReinitializeCOMInterfaces();

    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();
    
    // Hotkey related
    int hotkeyModifiers;
    int hotkeyVK;
    std::function<void()> restartAudioEngineCallback;
    RAIIHandle hotkeyHandle{nullptr};
    bool InitializeHotkey();
    void CleanupHotkey();
    std::thread hotkeyThread;
    std::atomic<bool> hotkeyRunning{false};
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND hwndHotkeyWindow = nullptr;

    static std::string WideStringToUTF8(const std::wstring& wideStr);

    std::atomic<ULONG> refCount;
    std::vector<std::function<void(float, bool)>> callbacks;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    Microsoft::WRL::ComPtr<IMMDevice> speakers;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume;

    // RAII wrapper for HANDLE
    RAIIHandle comInitMutex;
    bool comInitialized;
    std::string targetDeviceUUID;


    mutable std::mutex comInitializedMutex; // Protects comInitialized flag
    mutable std::mutex soundMutex;          // Protects sound-related operations
    mutable std::mutex callbackMutex;       // Protects callbacks vector


};

#endif // WINDOWSMANAGER_H
