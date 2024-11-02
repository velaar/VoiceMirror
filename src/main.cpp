#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "COMUtilities.h"
#include "ChannelUtility.h"
#include "DeviceMonitor.h"
#include "Logger.h"
#include "Defconf.h"
#include "VoicemeeterAPI.h"
#include "VoicemeeterManager.h"
#include "ConfigParser.h"
#include "VolumeMirror.h"
#include "cxxopts.hpp"
#include <Functiondiscoverykeys_devpkey.h>

using namespace std::string_view_literals;
using UniqueHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;

using Microsoft::WRL::ComPtr;

std::atomic<bool> g_running(true);

UniqueHandle g_hQuitEvent = nullptr;

std::mutex cv_mtx;
std::condition_variable cv;
bool exitFlag = false;

bool InitializeQuitEvent() {
    g_hQuitEvent.reset(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!g_hQuitEvent) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return false;
    }
    Logger::Instance().Log(LogLevel::DEBUG, "Quit event created or opened successfully.");
    return true;
}

void TranslateStructuredException(unsigned int code, EXCEPTION_POINTERS *) {
    throw std::runtime_error("Structured Exception: " + std::to_string(code));
}

void WaitForQuitEvent() {
    if (g_hQuitEvent) {
        Logger::Instance().Log(LogLevel::DEBUG, "Waiting for quit event signal...");
        WaitForSingleObject(g_hQuitEvent.get(), INFINITE);
        Logger::Instance().Log(LogLevel::DEBUG, "Quit event signaled. Initiating shutdown sequence...");
        g_running = false;  // Set to false to trigger main loop exit

        {
            std::lock_guard<std::mutex> lock(cv_mtx);
            exitFlag = true;
        }
        cv.notify_one();
    } else {
        Logger::Instance().Log(LogLevel::ERR, "Quit event handle is null; unable to wait for quit event.");
    }
}

void ListMonitorableDevices() {
    if (!InitializeCOM()) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize COM library.");
        return;
    }

    ComPtr<IMMDeviceEnumerator> pEnumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), &pEnumerator);
    if (FAILED(hr)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create MMDeviceEnumerator.");
        UninitializeCOM();
        return;
    }

    ComPtr<IMMDeviceCollection> pCollection;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to enumerate audio endpoints.");
        UninitializeCOM();
        return;
    }

    UINT count = 0;
    pCollection->GetCount(&count);

    Logger::Instance().Log(LogLevel::INFO, "Available audio devices for monitoring:");
    for (UINT i = 0; i < count; i++) {
        ComPtr<IMMDevice> pDevice;
        hr = pCollection->Item(i, &pDevice);
        if (SUCCEEDED(hr)) {
            ComPtr<IPropertyStore> pProps;
            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
            if (SUCCEEDED(hr)) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr)) {
                    LPWSTR deviceId = nullptr;
                    pDevice->GetId(&deviceId);

                    std::string deviceUUID;
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, deviceId, -1, NULL, 0, NULL, NULL);
                    deviceUUID.resize(size_needed - 1);
                    WideCharToMultiByte(CP_UTF8, 0, deviceId, -1, &deviceUUID[0], size_needed, NULL, NULL);

                    std::string deviceName;
                    size_needed = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, NULL, 0, NULL, NULL);
                    deviceName.resize(size_needed - 1);
                    WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &deviceName[0], size_needed, NULL, NULL);

                    Logger::Instance().Log(LogLevel::INFO, "----------------------------------------");
                    Logger::Instance().Log(LogLevel::INFO, "Device " + std::to_string(i) + ":");
                    Logger::Instance().Log(LogLevel::INFO, "Name: " + deviceName);
                    Logger::Instance().Log(LogLevel::INFO, "UUID: " + deviceUUID);

                    PropVariantClear(&varName);
                    CoTaskMemFree(deviceId);
                }
            }
        }
    }

    UninitializeCOM();
}

int main(int argc, char *argv[]) {
    std::unordered_map<std::string, std::string> configMap;
    std::ofstream logFileStream;  

    UniqueHandle hMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!hMutex) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create mutex.");
        return 1;  // Exit cleanly
    }

    if (!InitializeQuitEvent()) {
        Logger::Instance().Log(LogLevel::ERR, "Unable to initialize quit event.");
        return -1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        UniqueHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent) {
            SetEvent(hQuitEvent.get());
            Logger::Instance().Log(LogLevel::INFO, "Signaled existing instance to quit.");
            return 0;  
        }
        Logger::Instance().Log(LogLevel::ERR, "Failed to signal existing instance.");
        return 1;
    }

    cxxopts::Options options = ConfigParser::CreateOptions();
    cxxopts::ParseResult result;
    try {
        options.positional_help("[optional args]")
            .show_positional_help();

        options.allow_unrecognised_options();
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::parsing &e) {
        Logger::Instance().Log(LogLevel::ERR, "Unrecognized option or argument error: " + std::string(e.what()));
        Logger::Instance().Log(LogLevel::INFO, "Use --help to see available options.");
        return -1;
    } catch (const std::exception &e) {
        Logger::Instance().Log(LogLevel::ERR, "Error parsing options: " + std::string(e.what()));
        Logger::Instance().Log(LogLevel::INFO, "Use --help to see available options.");
        return -1;
    }

    Config config;
    ConfigParser::ValidateOptions(result);
    ConfigParser::ValidateToggleConfig(config);

    if (result.count("config")) {
        config.configFilePath = result["config"].as<std::string>();
    } else {
        config.configFilePath = "VoiceMirror.conf";
    }

    ConfigParser::HandleConfiguration(config.configFilePath, config);
    ConfigParser::ApplyCommandLineOptions(result, config);

    if (!ConfigParser::SetupLogging(config)) {
        return -1;
    }

    if (ConfigParser::HandleSpecialCommands(config)) {
        return 0;
    }

    if (config.hideConsole) {
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (FreeConsole() == 0) {
                Logger::Instance().Log(LogLevel::ERR, "Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            Logger::Instance().Log(LogLevel::ERR, "Failed to get console window handle.");
        }
    }

    if (config.listMonitor) {
        ListMonitorableDevices();
        return 0;
    }

    VoicemeeterManager vmrManager;
    vmrManager.SetDebugMode(config.debug);

    if (!vmrManager.Initialize(config.voicemeeterType)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize Voicemeeter Manager.");
        return -1;
    }

    if (config.listInputs) {
        ChannelUtility::ListInputs(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    if (config.listOutputs) {
        ChannelUtility::ListOutputs(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    if (config.listChannels) {
        ChannelUtility::ListAllChannels(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    int channelIndex = config.index;
    std::string typeStr = config.type;
    float minDbm = config.minDbm;
    float maxDbm = config.maxDbm;

    // Handle monitor and toggle parameters
    std::string monitorDeviceUUID = config.monitorDeviceUUID;
    bool isMonitoring = !monitorDeviceUUID.empty();

    ToggleConfig toggleConfig;
    bool isToggleEnabled = false;
    if (!config.toggleParam.empty()) {
        toggleConfig = ConfigParser::ParseToggleParameter(config.toggleParam);
        isToggleEnabled = true;
    }

    std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
    std::unique_ptr<VolumeMirror> mirror = nullptr;
    try {
            // Before creating the VolumeMirror instance, convert the string to ChannelType
            ChannelType channelType;
            if (typeStr == "input") {
                channelType = ChannelType::Input;
            } else if (typeStr == "output") {
                channelType = ChannelType::Output;
            } else {
                Logger::Instance().Log(LogLevel::ERR, "Invalid channel type: " + typeStr);
                return -1;
            }

            // Then update the VolumeMirror creation to use the enum
        mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, config.sound);
        mirror->SetPollingMode(config.pollingEnabled, config.pollingInterval);
        mirror->Start();
        Logger::Instance().Log(LogLevel::INFO, "Volume mirroring started.");

        if (isMonitoring) {
            deviceMonitor = std::make_unique<DeviceMonitor>(monitorDeviceUUID, toggleConfig, vmrManager, *mirror);
            Logger::Instance().Log(LogLevel::INFO, "Started monitoring device UUID: " + monitorDeviceUUID);
        }

        Logger::Instance().Log(LogLevel::INFO, "VoiceMirror is running. Press Ctrl+C to exit.");

        std::unique_ptr<std::thread> quitThread;
        if (isMonitoring) {
            quitThread = std::make_unique<std::thread>(WaitForQuitEvent);
        }

        {
            std::unique_lock<std::mutex> lock(cv_mtx);
            cv.wait(lock, [] { return !g_running.load(); });
        }

        if (mirror) {
            mirror->Stop();
        }
        deviceMonitor.reset();
        mirror.reset();
        vmrManager.Shutdown();

        Logger::Instance().Log(LogLevel::INFO, "VoiceMirror has shut down gracefully.");
    } catch (const std::exception &ex) {
        Logger::Instance().Log(LogLevel::ERR, "An error occurred: " + std::string(ex.what()));
        vmrManager.Shutdown();
        return -1;
    }
    return 0;
}

void signalHandler(int signum) {
    Logger::Instance().Log(LogLevel::INFO, "Interrupt signal (" + std::to_string(signum) + ") received. Shutting down...");
    g_running = false;
    cv.notify_one();
}

