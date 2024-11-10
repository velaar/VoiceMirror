#ifndef WINDOWSMANAGER_H
#define WINDOWSMANAGER_H

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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

    void ListMonitorableDevices();

    // Public methods for playing sounds
    void PlayStartupSound();
    void PlaySyncSound();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG)
    AddRef() override;
    STDMETHODIMP_(ULONG)
    Release() override;

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

    void CheckDevice(LPCWSTR deviceId, bool isAdded);
    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();

    static std::string WideStringToUTF8(const std::wstring& wideStr);

    std::atomic<ULONG> refCount;
    mutable std::mutex soundMutex;
    std::mutex callbackMutex;
    std::vector<std::function<void(float, bool)>> callbacks;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    Microsoft::WRL::ComPtr<IMMDevice> speakers;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume;

    // RAII wrapper for HANDLE
    RAIIHandle comInitMutex;
    bool comInitialized;
    std::string targetDeviceUUID;
};

#endif  // WINDOWSMANAGER_H
