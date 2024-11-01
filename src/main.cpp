/**
 * @file main.cpp
 * @brief Main entry point for VoiceMirror application
 *
 * VoiceMirror synchronizes Windows audio volume with Voicemeeter channels.
 * Key features:
 * - Single instance management using Windows mutex
 * - Configuration via command line and config file
 * - Device monitoring and volume mirroring
 * - Graceful shutdown handling
 */

// Project-Specific Includes
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


/**
 * @struct HandleDeleter
 * @brief RAII wrapper for Windows HANDLE cleanup
 *
 * Ensures proper cleanup of Windows handles when they go out of scope
 */
struct HandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using UniqueHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;

// Using WRL::ComPtr for COM interfaces
using Microsoft::WRL::ComPtr;

// Global running flag for signal handling
std::atomic<bool> g_running(true);

// RAII-managed quit event handle
UniqueHandle g_hQuitEvent = nullptr;

// Condition variable and mutex for main loop synchronization
std::mutex cv_mtx;
std::condition_variable cv;
bool exitFlag = false;

/**
 * @brief Helper function to create or open the quit event.
 * @return true if the event was successfully created or opened, false otherwise.
 */
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

/**
 * @brief Function to wait for the quit event to be signaled.
 */
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
    /**
   * @brief Parses a key-value configuration file.
   * @param configPath Path to the config file.
   * @param configMap Map to store configuration parameters.
   */
void ParseConfigFile(const std::string &configPath, std::unordered_map<std::string, std::string> &configMap) {
    ConfigParser::ParseConfigFile(configPath, configMap);
}

/**
   * @brief Applies configuration parameters from the config file to the Config struct.
   * @param configMap Map containing configuration parameters.
 * @param config Config struct to be updated.
 */
void ApplyConfig(const std::unordered_map<std::string, std::string> &configMap, Config &config) {
    ConfigParser::ApplyConfig(configMap, config);
}

/**
 * @brief Performs additional validation on the parsed command-line options.
 * This function is called after parsing the command-line options but before applying them to the configuration.
 * It checks that the parsed option values are within the expected ranges and throws an exception if any are invalid.
 */
void ValidateOptions(const cxxopts::ParseResult &result) {
    ConfigParser::ValidateOptions(result);
}

/**
 * @brief Lists available audio devices for monitoring.
 */
void ListMonitorableDevices() {
    if (!InitializeCOM()) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize COM library.");
        return;
    }

    // Create MMDeviceEnumerator
    ComPtr<IMMDeviceEnumerator> pEnumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), &pEnumerator);
    if (FAILED(hr)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create MMDeviceEnumerator.");
        UninitializeCOM();
        return;
    }

    // Enumerate active audio rendering devices
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
    // Initialize variables for logging and config
    std::unordered_map<std::string, std::string> configMap;
    std::ofstream logFileStream;  // Log file stream

    // Create a named mutex to ensure single instance
    UniqueHandle hMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!hMutex) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create mutex.");
        return 1;  // Exit cleanly
    }

    // Attempt to initialize the quit event for signaling
    if (!InitializeQuitEvent()) {
        Logger::Instance().Log(LogLevel::ERR, "Unable to initialize quit event.");
        return -1;
    }

    // Adjustments to ensure only one instance runs at a time with retry logic
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance exists - signal it and exit normally
        UniqueHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent) {
            SetEvent(hQuitEvent.get());
            Logger::Instance().Log(LogLevel::INFO, "Signaled existing instance to quit.");
            return 0;  // Exit cleanly
        }
        Logger::Instance().Log(LogLevel::ERR, "Failed to signal existing instance.");
        return 1;
    }

    // Define command-line options
    cxxopts::Options options = ConfigParser::CreateOptions();    cxxopts::ParseResult result;
    try {
        options.positional_help("[optional args]")
            .show_positional_help();

        // Set parsing behavior to disallow unrecognized options
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

    try {
        ConfigParser::ValidateOptions(result);
    } catch (const std::exception &e) {
        Logger::Instance().Log(LogLevel::ERR, std::string("Invalid option value: ") + e.what());
        Logger::Instance().Log(LogLevel::INFO, "Use --help to see available options.");
        return -1;
    }
    // Initialize Config with default values
    Config config;

    // Set default config file path
    if (result.count("config")) {
        config.configFilePath = result["config"].as<std::string>();
    } else {
        config.configFilePath = "VoiceMirror.conf";
    }

    // Parse config file if it exists
    ConfigParser::ParseConfigFile(config.configFilePath, configMap);
    ConfigParser::ApplyConfig(configMap, config);

    // Update Config with command-line options (overrides config file)
    ConfigParser::ApplyCommandLineOptions(result, config);

    // Set up logging
    if (config.debug) {
        Logger::Instance().SetLogLevel(LogLevel::DEBUG);
    } else {
        Logger::Instance().SetLogLevel(LogLevel::INFO);
    }

    if (config.loggingEnabled) {
        Logger::Instance().EnableFileLogging(config.logFilePath);
    }

    // Handle help
    if (config.help) {
        Logger::Instance().Log(LogLevel::INFO, options.help());
        return 0;
    }

    // Handle version
    if (config.version) {
        Logger::Instance().Log(LogLevel::INFO, "VoiceMirror Version 0.2.0-alpha");
        return 0;
    }

    // Handle hidden console window
    if (config.hideConsole) {
        if (!config.loggingEnabled) {
            Logger::Instance().Log(LogLevel::ERR, "--hidden option requires --log to be specified.");
            return -1;
        }

        // Hide the console window
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (FreeConsole() == 0) {
                Logger::Instance().Log(LogLevel::ERR, "Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            Logger::Instance().Log(LogLevel::ERR, "Failed to get console window handle.");
        }
    }

    // Handle shutdown
    if (config.shutdown) {
        // Attempt to signal any running instances to quit
        UniqueHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent) {
            if (!SetEvent(hQuitEvent.get())) {
                Logger::Instance().Log(LogLevel::ERR, "Failed to signal quit event to running instances.");
                return -1;
            }
            Logger::Instance().Log(LogLevel::INFO, "Signaled running instances to quit.");
        }
        {
            Logger::Instance().Log(LogLevel::INFO, "No running instances found.");
        }
        // Exit immediately
        return 0;
    }

    // Handle list monitorable devices
    if (config.listMonitor) {
        ListMonitorableDevices();
        return 0;
    }

    // Initialize VoicemeeterManager
    VoicemeeterManager vmrManager;    vmrManager.SetDebugMode(config.debug);

    if (!vmrManager.Initialize(config.voicemeeterType)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize Voicemeeter Manager.");
        return -1;
    }

    // Handle list inputs
    if (config.listInputs) {
        ChannelUtility::ListInputs(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // Handle list outputs
    if (config.listOutputs) {
        ChannelUtility::ListOutputs(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // Handle list channels
    if (config.listChannels) {
        ChannelUtility::ListAllChannels(vmrManager);
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // Get command-line options
    int channelIndex = config.index;
    std::string typeStr = config.type;
    float minDbm = config.minDbm;
    float maxDbm = config.maxDbm;

    // Validate 'type' option
    ChannelType channelType;
    if (typeStr == "input" || typeStr == "Input") {
        channelType = ChannelType::Input;
    } else if (typeStr == "output" || typeStr == "Output") {
        channelType = ChannelType::Output;
    } else {
        Logger::Instance().Log(LogLevel::ERR, "Invalid type specified. Use 'input' or 'output'.");
        vmrManager.Shutdown();
        return -1;
    }

    // Handle monitor and toggle parameters
    std::string monitorDeviceUUID = config.monitorDeviceUUID;
    bool isMonitoring = !monitorDeviceUUID.empty();

    ToggleConfig toggleConfig;
    bool isToggleEnabled = false;
    if (!config.toggleParam.empty()) {
        if (!isMonitoring) {
            Logger::Instance().Log(LogLevel::ERR, "--toggle parameter requires --monitor to be specified.");
            vmrManager.Shutdown();
            return -1;
        }

        std::string toggleParam = config.toggleParam;
        // Expected format: type:index1:index2 (e.g., input:0:1)
        size_t firstColon = toggleParam.find(':');
        size_t secondColon = toggleParam.find(':', firstColon + 1);

        if (firstColon == std::string::npos || secondColon == std::string::npos) {
            Logger::Instance().Log(LogLevel::ERR, "Invalid --toggle format. Expected format: type:index1:index2 (e.g., input:0:1)");
            vmrManager.Shutdown();
            return -1;
        }

        toggleConfig.type = toggleParam.substr(0, firstColon);
        std::string index1Str = toggleParam.substr(firstColon + 1, secondColon - firstColon - 1);
        std::string index2Str = toggleParam.substr(secondColon + 1);

        try {
            toggleConfig.index1 = std::stoi(index1Str);
            toggleConfig.index2 = std::stoi(index2Str);
            isToggleEnabled = true;
        } catch (const std::exception &ex) {
            Logger::Instance().Log(LogLevel::ERR, "Invalid indices in --toggle parameter: " + std::string(ex.what()));
            vmrManager.Shutdown();
            return -1;
        }

        // Validate toggle type
        if (toggleConfig.type != "input" && toggleConfig.type != "output") {
            Logger::Instance().Log(LogLevel::ERR, "Invalid toggle type: " + toggleConfig.type + ". Use 'input' or 'output'.");
            vmrManager.Shutdown();
            return -1;
        }
    }
    std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
    std::unique_ptr<VolumeMirror> mirror = nullptr;

    // Initialize VolumeMirror
    try {
        mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, config.sound);
        mirror->SetPollingMode(config.pollingEnabled, config.pollingInterval);
        mirror->Start();
        Logger::Instance().Log(LogLevel::INFO, "Volume mirroring started.");

        // Initialize DeviceMonitor if monitoring is enabled
        if (isMonitoring) {
            deviceMonitor = std::make_unique<DeviceMonitor>(monitorDeviceUUID, toggleConfig, vmrManager, *mirror);

            try {
                Logger::Instance().Log(LogLevel::INFO, "Started monitoring device UUID: " + monitorDeviceUUID);
            } catch (const std::exception &ex) {
                Logger::Instance().Log(LogLevel::ERR, "Failed to initialize DeviceMonitor: " + std::string(ex.what()));
                return -1;
            }
        }

        Logger::Instance().Log(LogLevel::INFO, "VoiceMirror is running. Press Ctrl+C to exit.");

        // Start a thread to wait for the quit event
        std::unique_ptr<std::thread> quitThread;
        if (isMonitoring) {
            quitThread = std::make_unique<std::thread>(WaitForQuitEvent);  // Only create if needed
        }

        // Main loop to keep the application running until Ctrl+C is pressed
        {
            std::unique_lock<std::mutex> lock(cv_mtx);
            cv.wait(lock, [] { return !g_running.load(); });
        }

        // Clean shutdown sequence
        if (mirror) {
            mirror->Stop();
        }
        deviceMonitor.reset();  // Release DeviceMonitor first
        mirror.reset();         // Release VolumeMirror
        vmrManager.Shutdown();  // Finally shutdown Voicemeeter

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
    cv.notify_one();  // Notify the main loop to exit
}
