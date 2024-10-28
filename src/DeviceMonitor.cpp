// DeviceMonitor.cpp
#include "DeviceMonitor.h"
#include <chrono>
#include <stdexcept>
#include <windows.h>
#include <Functiondiscoverykeys_devpkey.h>

DeviceMonitor::DeviceMonitor(const std::string& deviceUUID, ToggleConfig toggleConfig, VoicemeeterManager& manager, VolumeMirror& mirror)
    : targetDeviceUUID(deviceUUID), toggleConfig(toggleConfig),  voicemeeterManager(manager), debugMode(manager.GetDebugMode()) ,volumeMirror(mirror), refCount(1), isToggled(false) {
    if (!InitializeCOM()) {
        throw std::runtime_error("Failed to initialize COM library for DeviceMonitor.");
    }

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr)) {
        UninitializeCOM();
        throw std::runtime_error("Failed to create MMDeviceEnumerator for DeviceMonitor.");
    }

    hr = deviceEnumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
        deviceEnumerator->Release();
        UninitializeCOM();
        throw std::runtime_error("Failed to register Endpoint Notification Callback.");
    }

    if (debugMode) { //debug
        std::cout << "[DEBUG] DeviceMonitor initialized for UUID: " << targetDeviceUUID << std::endl;
    }
}

DeviceMonitor::~DeviceMonitor() {
    if (deviceEnumerator) {
        deviceEnumerator->UnregisterEndpointNotificationCallback(this);
        deviceEnumerator->Release();
    }
    UninitializeCOM();

    if (debugMode) {
        std::cout << "[DEBUG] DeviceMonitor destroyed for UUID: " << targetDeviceUUID << std::endl;
    }
}

STDMETHODIMP DeviceMonitor::QueryInterface(REFIID riid, void** ppvInterface) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
        *ppvInterface = this;
        AddRef();
        return S_OK;
    }
    *ppvInterface = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DeviceMonitor::AddRef() {
    return refCount.fetch_add(1) + 1;
}

STDMETHODIMP_(ULONG) DeviceMonitor::Release() {
    ULONG count = refCount.fetch_sub(1) - 1;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP DeviceMonitor::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    if (debugMode) {
        std::wcout << L"[DEBUG] OnDeviceStateChanged called for Device ID: " << pwstrDeviceId << L", New State: " << dwNewState << std::endl;
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
        std::wcout << L"[DEBUG] OnDeviceAdded called for Device ID: " << pwstrDeviceId << std::endl;
    }
    CheckDevice(pwstrDeviceId, true);
    return S_OK;
}

STDMETHODIMP DeviceMonitor::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    if (debugMode) {
        std::wcout << L"[DEBUG] OnDeviceRemoved called for Device ID: " << pwstrDeviceId << std::endl;
    }
    CheckDevice(pwstrDeviceId, false);
    return S_OK;
}

STDMETHODIMP DeviceMonitor::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, [[maybe_unused]] const PROPERTYKEY key) {
    if (debugMode) {
        std::wcout << L"[DEBUG] OnPropertyValueChanged called for Device ID: " << pwstrDeviceId << std::endl;
    }
    return S_OK;
}

STDMETHODIMP DeviceMonitor::OnDefaultDeviceChanged([[maybe_unused]] EDataFlow flow, [[maybe_unused]] ERole role, LPCWSTR pwstrDefaultDeviceId) {
    if (debugMode) {
        std::wcout << L"[DEBUG] OnDefaultDeviceChanged called for Device ID: " << pwstrDefaultDeviceId << std::endl;
    }
    return S_OK;
}

void DeviceMonitor::CheckDevice(LPCWSTR deviceId, bool isAdded) {
    std::wstring ws(deviceId);
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), NULL, 0, NULL, NULL);
    std::string deviceUUID(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), &deviceUUID[0], size_needed, NULL, NULL);

    if (debugMode) {
        std::cout << "[DEBUG] Checking device UUID: " << deviceUUID << std::endl;
    }

    if (deviceUUID == targetDeviceUUID) {
        if (isAdded) {
            std::cout << "Monitored device (UUID: " << targetDeviceUUID << ") has been plugged in." << std::endl;
            HandleDevicePluggedIn();
        } else {
            std::cout << "Monitored device (UUID: " << targetDeviceUUID << ") has been unplugged." << std::endl;
            HandleDeviceUnplugged();
        }
    }
}

void DeviceMonitor::HandleDevicePluggedIn() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    if (debugMode) {
        std::cout << "Handling device plugged in..." << std::endl;
    }

    // Use the manager to restart the audio engine with default delays (2 seconds before and after)
    voicemeeterManager.RestartAudioEngine();

    if (!toggleConfig.type.empty()) {
        if (debugMode) {
            std::cout << "Handling toggle logic in..." << std::endl;
        }
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2, true);
    }
}

void DeviceMonitor::HandleDeviceUnplugged() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    if (debugMode) {
        std::cout << "Handling device unplugged..." << std::endl;
    }

    if (!toggleConfig.type.empty()) {
        if (debugMode) {
            std::cout << "Toggling mute off for type: " << toggleConfig.type 
                      << " index1: " << toggleConfig.index1 
                      << " index2: " << toggleConfig.index2 << std::endl;
        }
        ToggleMute(toggleConfig.type, toggleConfig.index1, toggleConfig.index2, false);
    } else {
        if (debugMode) {
            std::cout << "Toggle configuration is empty. Skipping toggle mute." << std::endl;
        }
    }
}


bool DeviceMonitor::IsMonitoredDevice(LPCWSTR deviceId) {
    std::wstring ws(deviceId);
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), NULL, 0, NULL, NULL);
    std::string deviceUUID(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), &deviceUUID[0], size_needed, NULL, NULL);
    //return deviceUUID == targetDeviceUUID;
    return true;
}


void DeviceMonitor::ToggleMute(const std::string& type, int index1, int index2, bool isPluggedIn) {
    std::string muteParam1, muteParam2;

    if (type == "input") {
        muteParam1 = "Strip[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Strip[" + std::to_string(index2) + "].Mute";
    } else if (type == "output") {
        muteParam1 = "Bus[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Bus[" + std::to_string(index2) + "].Mute";
    } else {
        std::cerr << "Invalid toggle type: " << type << std::endl;
        return;
    }

    if (isPluggedIn) {
        // On device plug, unmute index1 and mute index2
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 0.0f); // Unmute index1
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 1.0f); // Mute index2
        std::cout << "Device Plugged: Unmuted " << type << ":" << index1 << ", Muted " << type << ":" << index2 << std::endl;
        isToggled = true;
    } else {
        // On device unplug, mute index1 and unmute index2
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 1.0f); // Mute index1
        voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 0.0f); // Unmute index2
        std::cout << "Device Unplugged: Muted " << type << ":" << index1 << ", Unmuted " << type << ":" << index2 << std::endl;
        isToggled = false;
    }
}


/* old implementation - will keep for better times
void DeviceMonitor::ToggleMute(const std::string& type, int index1, int index2, bool isPluggedIn) {
    std::string muteParam1, muteParam2;

    if (type == "input") {
        muteParam1 = "Strip[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Strip[" + std::to_string(index2) + "].Mute";
    } else if (type == "output") {
        muteParam1 = "Bus[" + std::to_string(index1) + "].Mute";
        muteParam2 = "Bus[" + std::to_string(index2) + "].Mute";
    } else {
        std::cerr << "Invalid toggle type: " << type << std::endl;
        return;
    }

    float muteVal1 = 0.0f, muteVal2 = 0.0f;
    if (voicemeeterManager.GetAPI().GetParameterFloat(muteParam1.c_str(), &muteVal1) != 0)
        muteVal1 = 0.0f;
    if (voicemeeterManager.GetAPI().GetParameterFloat(muteParam2.c_str(), &muteVal2) != 0)
        muteVal2 = 0.0f;

    if (isPluggedIn) {
        if (muteVal1 != 0.0f) {
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 0.0f);
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 1.0f);
            std::cout << "Toggled Mute: Unmuted " << type << ":" << index1 << ", Muted " << type << ":" << index2 << std::endl;
            isToggled = true;
        } else {
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 1.0f);
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 0.0f);
            std::cout << "Toggled Mute: Muted " << type << ":" << index1 << ", Unmuted " << type << ":" << index2 << std::endl;
            isToggled = false;
        }
    } else {
        if (isToggled) {
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 1.0f);
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 0.0f);
            std::cout << "Reversed Mute: Muted " << type << ":" << index1 << ", Unmuted " << type << ":" << index2 << std::endl;
            isToggled = false;
        } else {
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam1.c_str(), 0.0f);
            voicemeeterManager.GetAPI().SetParameterFloat(muteParam2.c_str(), 1.0f);
            std::cout << "Reversed Mute: Unmuted " << type << ":" << index1 << ", Muted " << type << ":" << index2 << std::endl;
            isToggled = true;
        }
    }
}
*/