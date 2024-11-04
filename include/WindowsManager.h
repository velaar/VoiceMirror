#ifndef WINDOWS_MANAGER_H
#define WINDOWS_MANAGER_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include "VoicemeeterManager.h"
#include "Defconf.h"


class WindowsManager : public IMMNotificationClient, public IAudioEndpointVolumeCallback {
public:
    WindowsManager(const std::string& deviceUUID, ToggleConfig toggleConfig, VoicemeeterManager& manager);
    ~WindowsManager();

    // COM initialization and cleanup
    bool InitializeCOM();
    void UninitializeCOM();

    // Device state change handling
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;

    // Audio control
    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    // Volume change callback registration
    void RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);
    void UnregisterVolumeChangeCallback(std::function<void(float, bool)> callback);

    // Reference counting (IUnknown)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // Volume notification
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

private:
    // COM interfaces initialization
    bool InitializeCOMInterfaces();
    void Cleanup();
    void ReinitializeCOMInterfaces();

    // Device monitoring functions
    void CheckDevice(LPCWSTR deviceId, bool isAdded);
    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();
    void ToggleMute(const std::string& type, int index1, int index2, bool isPluggedIn);

    // Member variables
    std::atomic<ULONG> refCount;
    HANDLE comInitMutex; 
    bool comInitialized;
    std::string targetDeviceUUID;
    ToggleConfig toggleConfig;
    VoicemeeterManager& voicemeeterManager;
    bool isToggled;

    // Callback related
    std::mutex callbackMutex;
    std::vector<std::function<void(float, bool)>> callbacks;

    // COM pointers for device management
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    Microsoft::WRL::ComPtr<IMMDevice> speakers;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume;
};

#endif // WINDOWS_MANAGER_H
