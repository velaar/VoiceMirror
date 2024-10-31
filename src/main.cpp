// main.cpp
// Project-Specific Includes
#include "COMUtilities.h"
#include "DeviceMonitor.h"
#include "Logger.h"
#include "VoicemeeterAPI.h"
#include "VoicemeeterManager.h"
#include "VolumeMirror.h"
#include "cxxopts.hpp"

#include <Functiondiscoverykeys_devpkey.h>
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
#include <thread>
#include <unordered_map>



// Define unique names for the mutex and event
constexpr char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";      // Mutex name
constexpr char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";  // Quit event name

// RAII Wrapper for Windows HANDLE
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

/**
 * @brief Signal handler for graceful shutdown.
 * @param signum The signal number.
 */
void signalHandler(int signum) {
    Logger::Instance().Log(LogLevel::INFO, "Interrupt signal (" + std::to_string(signum) + ") received. Shutting down...");
    g_running = false;
    cv.notify_one();  // Notify the main loop to exit
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
 * @return true if successful, false otherwise.
 */
bool ParseConfigFile(const std::string &configPath, std::unordered_map<std::string, std::string> &configMap) {
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        Logger::Instance().Log(LogLevel::INFO, "Config file not found: " + configPath + ". Continuing with command line flags.");
        return false;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        // Find the position of the first '#' character
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            // Remove the comment part
            line = line.substr(0, commentPos);
        }

        // Trim leading and trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines after removing comments
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // Trim whitespace from key and value
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            configMap[key] = value;
        }
    }

    return true;
}

/**
 * @brief Configuration structure to store application parameters.
 */
struct Config {
    bool listMonitor = false;
    bool listInputs = false;
    bool listOutputs = false;
    bool listChannels = false;
    int index = 3;
    std::string type = "input";
    float minDbm = -60.0f;
    float maxDbm = 12.0f;
    int voicemeeterType = 2;
    bool debug = false;
    bool version = false;
    bool help = false;
    bool sound = false;
    std::string monitorDeviceUUID;
    std::string logFilePath;
    bool loggingEnabled = false;
    bool hideConsole = false;
    std::string configFilePath;
    std::string toggleParam;
    bool shutdown = false;
    bool pollingEnabled = false;
    int pollingInterval = 100;
};

/**
 * @brief Applies configuration parameters from the config file to the Config struct.
 * @param configMap Map containing configuration parameters.
 * @param config Config struct to be updated.
 */
void ApplyConfig(const std::unordered_map<std::string, std::string> &configMap, Config &config) {
    for (const auto &kv : configMap) {
        const std::string &key = kv.first;
        const std::string &value = kv.second;

        if (key == "list-monitor") {
            config.listMonitor = (value == "true" || value == "1");
        } else if (key == "list-inputs") {
            config.listInputs = (value == "true" || value == "1");
        } else if (key == "list-outputs") {
            config.listOutputs = (value == "true" || value == "1");
        } else if (key == "list-channels") {
            config.listChannels = (value == "true" || value == "1");
        } else if (key == "index") {
            config.index = std::stoi(value);
        } else if (key == "type") {
            config.type = value;
        } else if (key == "min") {
            config.minDbm = std::stof(value);
        } else if (key == "max") {
            config.maxDbm = std::stof(value);
        } else if (key == "voicemeeter") {
            config.voicemeeterType = std::stoi(value);
        } else if (key == "debug") {
            config.debug = (value == "true" || value == "1");
        } else if (key == "sound") {
            config.sound = (value == "true" || value == "1");
        } else if (key == "monitor") {
            config.monitorDeviceUUID = value;
        } else if (key == "log") {
            config.loggingEnabled = true;
            config.logFilePath = value;
        } else if (key == "hidden") {
            config.hideConsole = (value == "true" || value == "1");
        } else if (key == "toggle") {
            config.toggleParam = value;
        } else if (key == "shutdown") {
            config.shutdown = (value == "true" || value == "1");
        } else if (key == "polling") {
            config.pollingEnabled = true;
            config.pollingInterval = std::stoi(value);
        }
    }
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
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize variables for logging and config
    std::unordered_map<std::string, std::string> configMap;
    std::ofstream logFileStream;  // Log file stream

    // Create a named mutex to ensure single instance
    UniqueHandle hMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!hMutex) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to create mutex.");
        return -1;
    }

    // Attempt to initialize the quit event for signaling
    if (!InitializeQuitEvent()) {
        Logger::Instance().Log(LogLevel::ERR, "Unable to initialize quit event.");
        return -1;
    }

    // Adjustments to ensure only one instance runs at a time with retry logic
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running. Signal it to quit.
        UniqueHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent) {
            if (!SetEvent(hQuitEvent.get())) {
                Logger::Instance().Log(LogLevel::ERR, "Failed to signal quit event to the first instance.");
                return -1;
            }
            Logger::Instance().Log(LogLevel::DEBUG, "Signaled quit event successfully. Waiting for first instance to exit.");

            // Wait for the first instance to release the mutex by acquiring it
            const int retryCount = 10;
            bool mutexAcquired = false;

            // Loop to repeatedly attempt to acquire the mutex with exponential backoff
            for (int i = 0; i < retryCount; ++i) {
                if (WaitForSingleObject(hMutex.get(), (i + 1) * 1000) == WAIT_OBJECT_0) {
                    mutexAcquired = true;
                    break;
                }
                Logger::Instance().Log(LogLevel::DEBUG, "Waiting for previous instance to release mutex... attempt " + std::to_string(i + 1));
            }

            if (!mutexAcquired) {
                Logger::Instance().Log(LogLevel::ERR, "Previous instance did not release mutex in a timely manner.");
                return -1;
            }
        } else {
            Logger::Instance().Log(LogLevel::ERR, "Failed to open quit event. Cannot ensure single-instance operation.");
            return -1;
        }
    } else if (g_hQuitEvent)  // Only create the quit event if the application is the first instance
    {
        // First instance setup
        Logger::Instance().Log(LogLevel::DEBUG, "First instance running, initializing quit event and waiting for signal.");

        if (!g_hQuitEvent) {
            Logger::Instance().Log(LogLevel::ERR, "Failed to create quit event for the first instance.");
            return -1;
        }
    }

    // Define command-line options
    cxxopts::Options options("VoiceMirror", "Synchronize Windows Volume with Voicemeeter virtual channels");

    options.add_options()
        ("C,list-channels", "List all Voicemeeter channels with their labels and exit")
        ("H,hidden", "Hide the console window. Use with --log to run without showing the console.")
        ("I,list-inputs", "List available Voicemeeter virtual inputs and exit")
        ("M,list-monitor", "List monitorable audio devices and exit")
        ("O,list-outputs", "List available Voicemeeter virtual outputs and exit")
        ("S,sound", "Enable chime sound on sync from Voicemeeter to Windows")
        ("T,toggle", "Toggle mute between two channels when device is plugged/unplugged. Must use with -m // --monitor Format: type:index1:index2 (e.g., 'input:0:1')", cxxopts::value<std::string>())
        ("V,voicemeeter", "Specify which Voicemeeter to use (1: Voicemeeter, 2: Banana, 3: Potato) (default: 2)", cxxopts::value<int>())
        ("c,config", "Specify a configuration file to manage application parameters.", cxxopts::value<std::string>())
        ("d,debug", "Enable debug mode for extensive logging")
        ("h,help", "Show help message and exit")
        ("i,index", "Specify the Voicemeeter virtual channel index to use (default: 3)", cxxopts::value<int>())
        ("l,log", "Enable logging to a file. Optionally specify a log file path.", cxxopts::value<std::string>())
        ("m,monitor", "Monitor a specific audio device by UUID and restart audio engine on plug/unplug events", cxxopts::value<std::string>())
        ("max", "Maximum dBm for Voicemeeter channel (default: 12.0)", cxxopts::value<float>())
        ("min", "Minimum dBm for Voicemeeter channel (default: -60.0)", cxxopts::value<float>())
        ("p,polling", "Enable polling mode with optional interval in milliseconds (default: 100ms)", cxxopts::value<int>()->implicit_value("100"))
        ("s,shutdown", "Shutdown all instances of the app and exit immediately")
        ("t,type", "Specify the type of channel to use ('input' or 'output') (default: 'input')", cxxopts::value<std::string>())
        ("v,version", "Show program's version number and exit");


    cxxopts::ParseResult result;
    try {
        auto result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::parsing &e) {
        Logger::Instance().Log(LogLevel::ERR, "Unrecognized option or argument error: " + std::string(e.what()));
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
    if (ParseConfigFile(config.configFilePath, configMap)) {
        ApplyConfig(configMap, config);
        Logger::Instance().Log(LogLevel::INFO, "Configuration file parsed successfully.");
    }

    // Update Config with command-line options (overrides config file)
    if (result.count("list-monitor"))
        config.listMonitor = true;
    if (result.count("list-inputs"))
        config.listInputs = true;
    if (result.count("list-outputs"))
        config.listOutputs = true;
    if (result.count("list-channels"))
        config.listChannels = true;
    if (result.count("index"))
        config.index = result["index"].as<int>();
    if (result.count("type"))
        config.type = result["type"].as<std::string>();
    if (result.count("min"))
        config.minDbm = result["min"].as<float>();
    if (result.count("max"))
        config.maxDbm = result["max"].as<float>();
    if (result.count("voicemeeter"))
        config.voicemeeterType = result["voicemeeter"].as<int>();
    if (result.count("debug"))
        config.debug = true;
    if (result.count("sound"))
        config.sound = true;
    if (result.count("monitor"))
        config.monitorDeviceUUID = result["monitor"].as<std::string>();
    if (result.count("log")) {
        config.loggingEnabled = true;
        config.logFilePath = result["log"].as<std::string>();
    }
    if (result.count("hidden"))
        config.hideConsole = true;
    if (result.count("toggle"))
        config.toggleParam = result["toggle"].as<std::string>();
    if (result.count("shutdown"))
        config.shutdown = true;
    if (result.count("help"))
        config.help = true;
    if (result.count("version"))
        config.version = true;
    if (result.count("polling")) {
        config.pollingEnabled = true;
        config.pollingInterval = result["polling"].as<int>();
    }

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
    VoicemeeterManager vmrManager;
    vmrManager.SetDebugMode(config.debug);

    if (!vmrManager.Initialize(config.voicemeeterType)) {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize Voicemeeter Manager.");
        return -1;
    }

    // Handle list inputs
    if (config.listInputs) {
        vmrManager.ListInputs();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // Handle list outputs
    if (config.listOutputs) {
        vmrManager.ListOutputs();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // Handle list channels
    if (config.listChannels) {
        vmrManager.ListAllChannels();
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

    // Initialize VolumeMirror
    try {
        VolumeMirror mirror(channelIndex, channelType, minDbm, maxDbm, vmrManager, config.sound);
        mirror.SetPollingMode(config.pollingEnabled, config.pollingInterval);  // Set polling mode

        mirror.Start();
        Logger::Instance().Log(LogLevel::INFO, "Volume mirroring started.");

        // Initialize DeviceMonitor if monitoring is enabled
        std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
        if (isMonitoring) {
            deviceMonitor = std::make_unique<DeviceMonitor>(monitorDeviceUUID, toggleConfig, vmrManager, mirror);

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
            // Wait for signal handler to set g_running to false
            cv.wait(lock, [] { return !g_running.load(); });
        }

        // Proper shutdown by setting API command and cleaning resources
        mirror.Stop();
        vmrManager.Shutdown();

        Logger::Instance().Log(LogLevel::INFO, "VoiceMirror has shut down gracefully.");
    } catch (const std::exception &ex) {
        Logger::Instance().Log(LogLevel::ERR, "An error occurred: " + std::string(ex.what()));
        vmrManager.Shutdown();
        return -1;
    }

    return 0;
}
