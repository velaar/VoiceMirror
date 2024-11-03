// WindowsVolumeManager.h
#ifndef WINDOWS_VOLUME_MANAGER_H
#define WINDOWS_VOLUME_MANAGER_H

#pragma once
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include <mutex>

#include "Logger.h"
#include "VolumeUtils.h"  // Include if needed for utility functions
#include "Defconf.h"      // Include Defconf.h for constants

using namespace Microsoft::WRL;

namespace VolumeUtils {

/**
 * @brief Manages Windows volume operations and handles volume change notifications.
 */
class WindowsVolumeManager : public IAudioEndpointVolumeCallback {
public:
    WindowsVolumeManager();
    ~WindowsVolumeManager();

    // Delete copy constructor and assignment operator
    WindowsVolumeManager(const WindowsVolumeManager&) = delete;
    WindowsVolumeManager& operator=(const WindowsVolumeManager&) = delete;

    // Volume control methods
    bool SetVolume(float volumePercent);
    bool SetMute(bool mute);
    float GetVolume() const;
    bool GetMute() const;

    // Callback registration
    void RegisterVolumeChangeCallback(const std::function<void(float, bool)>& callback);
    void UnregisterVolumeChangeCallback(const std::function<void(float, bool)>& callback);

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback method
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

private:
    // COM interfaces
    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    ComPtr<IMMDevice> speakers;
    ComPtr<IAudioEndpointVolume> endpointVolume;

    // Reference count for IUnknown
    std::atomic<ULONG> refCount;

    // Registered callbacks
    std::vector<std::function<void(float, bool)>> callbacks;
    mutable std::mutex callbackMutex;  // Protects access to callbacks vector

    // Helper method to initialize COM interfaces
    bool InitializeCOMInterfaces();
};

} // namespace VolumeUtils

#endif // WINDOWS_VOLUME_MANAGER_H
