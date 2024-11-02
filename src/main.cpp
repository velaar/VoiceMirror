#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

// Project-Specific Includes
#include "ConfigParser.h"        // Parsing and validating application configurations
#include "Defconf.h"             // Definitions (e.g., EVENT_NAME and MUTEX_NAME)
#include "DeviceMonitor.h"       // Manages monitoring of device states and events
#include "Logger.h"              // Logging utilities for debugging and information output
#include "RAIIHandle.h"          // RAII wrapper for HANDLEs
#include "VoicemeeterManager.h"  // Manages Voicemeeter instances and configuration
#include "VolumeMirror.h"        // Core mirroring functionality for audio volume levels
#include "VolumeUtils.h"         // Utility functions and definitions for volume controls
#include "cxxopts.hpp"           // Command-line parsing library for handling CLI options

// Aliases for improved code readability
using namespace std::string_view_literals;

// Global Variables
std::atomic<bool> g_running(true);
RAIIHandle g_hQuitEvent;  // RAIIHandle instance for quit event
std::mutex cv_mtx;
std::condition_variable cv;
bool exitFlag = false;

// Forward declaration of functions
bool InitializeQuitEvent();
void WaitForQuitEvent();
void signalHandler(int signum);

/**
 * @brief Helper function to set Windows volume (unchanged)
 */
bool SetWindowsVolume(int volumePercent) {
    // [Implementation remains the same as previously provided]
    // Placeholder implementation
    return true;
}

/**
 * @brief Helper function to play the startup sound (unchanged)
 */
void PlayStartupSound() {
    // [Implementation remains the same as previously provided]
    // Placeholder implementation
}

int main(int argc, char *argv[]) {
    // Parse command-line options and configurations first to set up logging
    cxxopts::Options options = ConfigParser::CreateOptions();

    cxxopts::ParseResult result;
    try {
        options.positional_help("[optional args]").show_positional_help();
        options.allow_unrecognised_options();
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::parsing &e) {
        std::cerr << "Unrecognized option or argument error: " << e.what() << std::endl;
        std::cerr << "Use --help to see available options." << std::endl;
        return -1;
    } catch (const std::exception &e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        std::cerr << "Use --help to see available options." << std::endl;
        return -1;
    }

    Config appConfig;
    try {
        ConfigParser::ValidateOptions(result);
    } catch (const std::exception &e) {
        std::cerr << "Validation error: " << e.what() << std::endl;
        return -1;
    }

    // Handle configuration file
    if (result.count("config")) {
        appConfig.configFilePath = result["config"].as<std::string>();
    } else {
        appConfig.configFilePath = DEFAULT_CONFIG_FILE;
    }

    // Parse and apply configuration from the config file and command-line options
    ConfigParser::HandleConfiguration(appConfig.configFilePath, appConfig);
    ConfigParser::ApplyCommandLineOptions(result, appConfig);

    if (!ConfigParser::SetupLogging(appConfig)) {
        std::cerr << "Failed to set up logging. Exiting application." << std::endl;
        return -1;
    }

    // Register signal handler for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create a mutex to ensure single instance
    RAIIHandle rawMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!rawMutex.get()) {
        LOG_ERROR("Failed to create mutex.");
        return 1;  // Exit cleanly
    }

    // Check if another instance is running
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        RAIIHandle existingQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (existingQuitEvent.get()) {
            if (!SetEvent(existingQuitEvent.get())) {
                LOG_ERROR("Failed to signal quit event to running instances.");
                return 1;
            }
            LOG_INFO("Signaled running instance to quit.");
            return 0;
        }
        LOG_ERROR("Failed to signal existing instance.");
        return 1;
    }

    // Initialize quit event
    if (!InitializeQuitEvent()) {
        LOG_ERROR("Failed to initialize quit event.");
        return 1;
    }

    if (ConfigParser::HandleSpecialCommands(appConfig)) {
        Logger::Instance().Shutdown();
        return 0;
    }

    if (appConfig.hideConsole) {
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (FreeConsole() == 0) {
                LOG_ERROR("Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            LOG_ERROR("Failed to get console window handle.");
        }
    }

    VoicemeeterManager vmrManager;
    vmrManager.SetDebugMode(appConfig.debug);

    if (!vmrManager.Initialize(appConfig.voicemeeterType)) {
        LOG_ERROR("Failed to initialize Voicemeeter Manager.");
        Logger::Instance().Shutdown();
        return -1;
    }

    if (appConfig.listMonitor) {
        vmrManager.ListMonitorableDevices();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return 0;
    }

    if (appConfig.listInputs) {
        vmrManager.ListInputs();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return 0;
    }

    if (appConfig.listOutputs) {
        vmrManager.ListOutputs();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return 0;
    }

    if (appConfig.listChannels) {
        vmrManager.ListAllChannels();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return 0;
    }

    if (appConfig.startupVolumePercent != -1) {
        if (!SetWindowsVolume(appConfig.startupVolumePercent)) {
            LOG_ERROR("Failed to set startup volume.");
        }

        if (appConfig.startupSound) {
            PlayStartupSound();
        }
    }

    int channelIndex = appConfig.index;
    std::string typeStr = appConfig.type;
    float minDbm = appConfig.minDbm;
    float maxDbm = appConfig.maxDbm;

    std::string monitorDeviceUUID = appConfig.monitorDeviceUUID;
    bool isMonitoring = !monitorDeviceUUID.empty();

    ToggleConfig toggleConfig;
    bool isToggleEnabled = false;
    if (!appConfig.toggleParam.empty()) {
        toggleConfig = ConfigParser::ParseToggleParameter(appConfig.toggleParam);
        isToggleEnabled = true;
    }

    std::unique_ptr<VolumeMirror> mirror = nullptr;
    std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
    try {
        VolumeUtils::ChannelType channelType;
        if (typeStr == "input") {
            channelType = VolumeUtils::ChannelType::Input;
        } else if (typeStr == "output") {
            channelType = VolumeUtils::ChannelType::Output;
        } else {
            LOG_ERROR("Invalid channel type: " + typeStr);
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return -1;
        }

        mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, appConfig.chime);
        mirror->SetPollingMode(appConfig.pollingEnabled, appConfig.pollingInterval);
        mirror->Start();
        LOG_INFO("Volume mirroring started.");

        if (isMonitoring) {
            deviceMonitor = std::make_unique<DeviceMonitor>(monitorDeviceUUID, toggleConfig, vmrManager, *mirror);
            LOG_INFO("Started monitoring device UUID: " + monitorDeviceUUID);
        }

        LOG_INFO("VoiceMirror is running. Press Ctrl+C to exit.");

        std::thread quitThread;
        if (isMonitoring) {
            quitThread = std::thread(WaitForQuitEvent);
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
        LOG_INFO("VoiceMirror has shut down gracefully.");

        Logger::Instance().Shutdown();

        if (quitThread.joinable()) {
            quitThread.join();
        }

    } catch (const std::exception &ex) {
        LOG_ERROR("An error occurred: " + std::string(ex.what()));
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return -1;
    }

    return 0;
}

bool InitializeQuitEvent() {
    g_hQuitEvent = RAIIHandle(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!g_hQuitEvent.get()) {
        LOG_ERROR("Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return false;
    }
    LOG_DEBUG("Quit event created or opened successfully.");
    return true;
}

void signalHandler(int signum) {
    LOG_INFO("Interrupt signal (" + std::to_string(signum) + ") received. Shutting down...");
    g_running = false;
    SetEvent(g_hQuitEvent.get());
    cv.notify_one();
}

void WaitForQuitEvent() {
    if (g_hQuitEvent.get()) {
        LOG_DEBUG("Waiting for quit event signal...");
        while (g_running.load()) {
            DWORD result = WaitForSingleObject(g_hQuitEvent.get(), 500);  // Check every 500 ms
            if (result == WAIT_OBJECT_0 || !g_running.load()) {
                LOG_DEBUG("Quit event signaled or running set to false. Initiating shutdown sequence...");
                g_running = false;
                {
                    std::lock_guard<std::mutex> lock(cv_mtx);
                    exitFlag = true;
                }
                cv.notify_one();
                break;
            }
        }
    } else {
        LOG_ERROR("Quit event handle is null; unable to wait for quit event.");
    }
}