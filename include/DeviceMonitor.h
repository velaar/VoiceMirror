#pragma once

#include <mmdeviceapi.h>
#include <atomic>
#include <mutex>
#include <string>
#include "VoicemeeterManager.h"
#include "VolumeMirror.h"
#include "ConfigParser.h"
#include <wrl.h>

// DeviceMonitor class to monitor audio device changes using Device UUID
class DeviceMonitor : public IMMNotificationClient {
public:
    DeviceMonitor(const std::string &deviceUUID, ToggleConfig toggleConfig,
                  VoicemeeterManager &manager, VolumeMirror &mirror);
    ~DeviceMonitor();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMMNotificationClient methods
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;

private:
    std::string targetDeviceUUID;
    ToggleConfig toggleConfig;
    VoicemeeterManager &voicemeeterManager;
    VolumeMirror &volumeMirror;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    std::atomic<ULONG> refCount;
    std::mutex toggleMutex;
    bool isToggled;

    void CheckDevice(LPCWSTR deviceId, bool isAdded);
    void HandleDevicePluggedIn();
    void HandleDeviceUnplugged();
    void ToggleMute(const std::string &type, int index1, int index2, bool isPluggedIn);
};
