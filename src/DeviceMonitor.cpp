// DeviceMonitor.cpp
#include "DeviceMonitor.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <windows.h>

#include <chrono>
#include <stdexcept>

#include "Logger.h"

DeviceMonitor::DeviceMonitor(const std::string &deviceUUID,
                             ToggleConfig toggleConfig,
                             VoicemeeterManager &manager, VolumeMirror &mirror)
    : targetDeviceUUID(deviceUUID), toggleConfig(toggleConfig), voicemeeterManager(manager), debugMode(manager.GetDebugMode()), volumeMirror(mirror), refCount(1), isToggled(false) {
    if (!InitializeCOM()) {
        throw std::runtime_error(
            "Failed to initialize COM library for DeviceMonitor.");
    }

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  (void **)&deviceEnumerator);
    if (FAILED(hr)) {
        UninitializeCOM();
        throw std::runtime_error(
            "Failed to create MMDeviceEnumerator for DeviceMonitor.");
    }

    hr = deviceEnumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
        deviceEnumerator->Release();
        UninitializeCOM();
        throw std::runtime_error(
            "Failed to register Endpoint Notification Callback.");
    }

    if (debugMode) {  // debug
        Logger::Instance().Log(LogLevel::DEBUG,
                               "DeviceMonitor initialized for UUID: " +
                                   targetDeviceUUID);
    }
}

DeviceMonitor::~DeviceMonitor() {
    if (deviceEnumerator) {
        deviceEnumerator->UnregisterEndpointNotificationCallback(this);
        deviceEnumerator->Release();
    }
    UninitializeCOM();

    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG,
                               "DeviceMonitor destroyed for UUID: " +
                                   targetDeviceUUID);
    }
}

STDMETHODIMP DeviceMonitor::QueryInterface(REFIID riid, void **ppvInterface) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
        *ppvInterface = this;
        AddRef();
        return S_OK;
    }
    *ppvInterface = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
DeviceMonitor::AddRef() {
    return refCount.fetch_add(1) + 1;
}

STDMETHODIMP_(ULONG)
DeviceMonitor::Release() {
    ULONG count = refCount.fetch_sub(1) - 1;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP DeviceMonitor::OnDeviceStateChanged(LPCWSTR pwstrDeviceId,
                                                 DWORD dwNewState) {
    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG,
                               "OnDeviceStateChanged called for Device ID: " +
                                   std::to_string(dwNewState));
    }

    if (dwNewState == 1) {
        CheckDevice(pwstrDeviceId, true);
    } else if (dwNewState == 4) {
        CheckDevice(pwstrDeviceId, false);
    }
    return S_OK;
}

STDMETHODIMP DeviceMonitor::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG, "OnDeviceAdded called.");
    }
    CheckDevice(pwstrDeviceId, true);
    return S_OK;
}

STDMETHODIMP DeviceMonitor::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG, "OnDeviceRemoved called.");
    }
    CheckDevice(pwstrDeviceId, false);
    return S_OK;
}

STDMETHODIMP
DeviceMonitor::OnPropertyValueChanged(LPCWSTR pwstrDeviceId,
                                      [[maybe_unused]] const PROPERTYKEY key) {
    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG, "OnPropertyValueChanged called.");
    }
    return S_OK;
}

STDMETHODIMP
DeviceMonitor::OnDefaultDeviceChanged([[maybe_unused]] EDataFlow flow,
                                      [[maybe_unused]] ERole role,
                                      LPCWSTR pwstrDefaultDeviceId) {
    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG, "OnDefaultDeviceChanged called.");
    }
    return S_OK;
}

void DeviceMonitor::CheckDevice(LPCWSTR deviceId, bool isAdded) {
    std::wstring ws(deviceId);
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(),
                                          (int)ws.length(), NULL, 0, NULL, NULL);
    std::string deviceUUID(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), &deviceUUID[0],
                        size_needed, NULL, NULL);

    if (debugMode) {
        Logger::Instance().Log(LogLevel::DEBUG,
                               "Checking device UUID: " + deviceUUID);
    }

    if (deviceUUID == targetDeviceUUID) {
        if (isAdded) {
            HandleDevicePluggedIn();
        } else {
            HandleDeviceUnplugged();
        }
    }
}

void DeviceMonitor::HandleDevicePluggedIn() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    Logger::Instance().Log(LogLevel::INFO,
                           "Monitored device has been plugged in.");

    // Use the manager to restart the audio engine with default delays
    voicemeeterManager.RestartAudioEngine();

    if (!toggleConfig.type.empty()) {
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2,
                   true);
    }
}

void DeviceMonitor::HandleDeviceUnplugged() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    Logger::Instance().Log(LogLevel::INFO,
                           "Monitored device has been unplugged.");

    if (!toggleConfig.type.empty()) {
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2,
                   false);
    }
}

void DeviceMonitor::ToggleMute(const std::string &type, int index1, int index2,
                               bool isPluggedIn) {
    std::string muteParam1, muteParam2;

    if (type == "input") {
        muteParam1 = "Strip[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Strip[" + std::to_string(index2) + "].Mute";
    } else if (type == "output") {
        muteParam1 = "Bus[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Bus[" + std::to_string(index2) + "].Mute";
    } else {
        Logger::Instance().Log(LogLevel::ERR, "Invalid toggle type: " + type);
        return;
    }

    if (isPluggedIn) {
        // On device plug, unmute index1 and mute index2
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(),
                                                      0.0f);  // Unmute index1
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(),
                                                      1.0f);  // Mute index2
        Logger::Instance().Log(LogLevel::INFO, "Device Plugged: Unmuted " + type +
                                                   ":" + std::to_string(index1) +
                                                   ", Muted " + type + ":" +
                                                   std::to_string(index2));
        isToggled = true;
    } else {
        // On device unplug, mute index1 and unmute index2
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(),
                                                      1.0f);  // Mute index1
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(),
                                                      0.0f);  // Unmute index2
        Logger::Instance().Log(LogLevel::INFO, "Device Unplugged: Muted " + type +
                                                   ":" + std::to_string(index1) +
                                                   ", Unmuted " + type + ":" +
                                                   std::to_string(index2));
        isToggled = false;
    }
}
