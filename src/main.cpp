// main.cpp

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

// Project-Specific Includes
#include "ConfigParser.h"          // Parsing and validating application configurations
#include "Defconf.h"               // Definitions (e.g., EVENT_NAME and MUTEX_NAME)
#include "DeviceMonitor.h"         // Manages monitoring of device states and events
#include "Logger.h"                // Logging utilities for debugging and information output
#include "RAIIHandle.h"            // RAII wrapper for HANDLEs
#include "VoicemeeterManager.h"    // Manages Voicemeeter instances and configuration
#include "VolumeMirror.h"          // Core mirroring functionality for audio volume levels
#include "VolumeUtils.h"           // Utility functions and definitions for volume controls
#include "WindowsVolumeManager.h"  // Windows-specific volume management
#include "cxxopts.hpp"             // Command-line parsing library for handling CLI options

using namespace std::string_view_literals;
using namespace std::string_literals;

// Forward declaration of functions
void PlayStartupSound();

// Application state encapsulation
class Application {
   public:
    Application() : g_running(true), exitFlag(false) {}
    std::atomic<bool> g_running;
    RAIIHandle g_hQuitEvent;
    std::mutex cv_mtx;
    std::condition_variable cv;
    bool exitFlag;
};

// Global pointer for signal handler to access Application state
Application *g_appStatePtr = nullptr;

// Initialize quit event with Application reference
bool InitializeQuitEvent(Application &appState) {
    appState.g_hQuitEvent = RAIIHandle(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!appState.g_hQuitEvent.get()) {
        LOG_ERROR("Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return false;
    }
    LOG_DEBUG("Quit event created or opened successfully.");
    return true;
}

// Signal handler needs to interact with Application state
void signalHandler(int signum) {
    if (g_appStatePtr) {
        LOG_INFO("Interrupt signal (" + std::to_string(signum) + ") received. Shutting down...");
        g_appStatePtr->g_running = false;
        SetEvent(g_appStatePtr->g_hQuitEvent.get());
        g_appStatePtr->cv.notify_one();
    }
}

void PlayStartupSound() {
    // Introduce a slight delay (e.g., 500 milliseconds) before playing the sound
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Define the path to the startup sound file
    // Ensure that "m95.mp3" is located in the same directory as the executable
    std::wstring soundFilePath = L"m95.mp3";

    // Define an alias for the media
    const wchar_t* aliasName = L"StartupSound";

    // Construct the MCI open command
    std::wstring openCommand = L"open \"" + soundFilePath + L"\" type mpegvideo alias " + aliasName;

    // Send the open command
    MCIERROR mciError = mciSendStringW(openCommand.c_str(), NULL, 0, NULL);
    if (mciError != 0) {
        wchar_t errorText[256];
        // Retrieve the error string
        if (mciGetErrorStringW(mciError, errorText, sizeof(errorText) / sizeof(wchar_t)) == 0) {
            // If retrieval fails, provide a generic error message
            LOG_ERROR("Failed to open audio file with MCI. Unknown error.");
            return;
        }

        // Convert wchar_t* to std::string using WideCharToMultiByte
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, errorText, -1, NULL, 0, NULL, NULL);
        std::string errorStr;
        if (sizeNeeded > 0) {
            errorStr.resize(sizeNeeded - 1); // Exclude null terminator
            WideCharToMultiByte(CP_UTF8, 0, errorText, -1, &errorStr[0], sizeNeeded, NULL, NULL);
        } else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to open audio file with MCI. Error: ") + errorStr);
        return;
    }

    // Send the play command
    std::wstring playCommand = L"play " + std::wstring(aliasName) + L" wait";
    mciError = mciSendStringW(playCommand.c_str(), NULL, 0, NULL);
    if (mciError != 0) {
        wchar_t errorText[256];
        if (mciGetErrorStringW(mciError, errorText, sizeof(errorText) / sizeof(wchar_t)) == 0) {
            LOG_ERROR("Failed to play audio file with MCI. Unknown error.");
            return;
        }

        // Convert wchar_t* to std::string using WideCharToMultiByte
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, errorText, -1, NULL, 0, NULL, NULL);
        std::string errorStr;
        if (sizeNeeded > 0) {
            errorStr.resize(sizeNeeded - 1); // Exclude null terminator
            WideCharToMultiByte(CP_UTF8, 0, errorText, -1, &errorStr[0], sizeNeeded, NULL, NULL);
        } else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to play audio file with MCI. Error: ") + errorStr);

        // Attempt to close the alias before returning
        std::wstring closeCommand = L"close " + std::wstring(aliasName);
        mciSendStringW(closeCommand.c_str(), NULL, 0, NULL);
        return;
    }

    // Send the close command to release resources
    std::wstring closeCommand = L"close " + std::wstring(aliasName);
    mciError = mciSendStringW(closeCommand.c_str(), NULL, 0, NULL);
    if (mciError != 0) {
        wchar_t errorText[256];
        if (mciGetErrorStringW(mciError, errorText, sizeof(errorText) / sizeof(wchar_t)) == 0) {
            LOG_ERROR("Failed to close audio file with MCI. Unknown error.");
            return;
        }

        // Convert wchar_t* to std::string using WideCharToMultiByte
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, errorText, -1, NULL, 0, NULL, NULL);
        std::string errorStr;
        if (sizeNeeded > 0) {
            errorStr.resize(sizeNeeded - 1); // Exclude null terminator
            WideCharToMultiByte(CP_UTF8, 0, errorText, -1, &errorStr[0], sizeNeeded, NULL, NULL);
        } else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to close audio file with MCI. Error: ") + errorStr);
        return;
    }

    LOG_DEBUG("Startup sound played successfully.");
}

int main(int argc, char *argv[]) {
    Application appState;
    g_appStatePtr = &appState;  // Assign the global pointer for signal handler

    // Instantiate ConfigParser with argc and argv
    ConfigParser parser(argc, argv);
    Config appConfig;

    try {
        // Handle configuration parsing (includes parsing config file and command-line options)
        parser.HandleConfiguration(DEFAULT_CONFIG_FILE, appConfig);
    } catch (const std::exception &e) {
        LOG_ERROR("Configuration error: " + std::string(e.what()));
        return EXIT_FAILURE;
    }

    // Special commands like --help, --version, and --shutdown are already handled within HandleConfiguration
    // Thus, no need to handle them again here

    // Register signal handlers for graceful shutdown
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
    if (!InitializeQuitEvent(appState)) {
        LOG_ERROR("Failed to initialize quit event.");
        return 1;
    }

    // Hide console if needed
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

    // Handle listing of monitors, inputs, outputs, and channels based on configuration
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

    // Set startup volume using WindowsVolumeManager
    if (appConfig.startupVolumePercent != -1) {
        try {
            VolumeUtils::WindowsVolumeManager volumeManager;
            if (appConfig.debug) {  // Add debug logging if debug mode is enabled
                LOG_DEBUG("Setting startup volume to " + std::to_string(appConfig.startupVolumePercent) + "%");
            }
            if (!volumeManager.SetVolume(static_cast<float>(appConfig.startupVolumePercent))) {
                LOG_ERROR("Failed to set startup volume.");
            } else {
                if (appConfig.debug) {
                    LOG_DEBUG("Startup volume set successfully.");
                }
            }

            if (appConfig.startupSound) {
                // Start the startup sound in a separate thread
                std::thread startupSoundThread(PlayStartupSound);
                startupSoundThread.detach();  // Detach the thread to run independently
            }
        } catch (const std::exception &ex) {
            LOG_ERROR(std::string("Exception while setting startup volume: ") + ex.what());
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
        try {
            toggleConfig = parser.ParseToggleParameter(appConfig.toggleParam);
            isToggleEnabled = true;
        } catch (const std::exception &ex) {
            LOG_ERROR(std::string("Exception while parsing toggle parameter: ") + ex.what());
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return -1;
        }
    }

    std::unique_ptr<VolumeMirror> mirror = nullptr;
    std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
    try {
        ChannelType channelType;
        if (typeStr == "input") {
            channelType = ChannelType::Input;
        } else if (typeStr == "output") {
            channelType = ChannelType::Output;
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
            quitThread = std::thread([&appState]() {
                while (appState.g_running.load()) {
                    DWORD result = WaitForSingleObject(appState.g_hQuitEvent.get(), 500);  // Check every 500 ms
                    if (result == WAIT_OBJECT_0 || !appState.g_running.load()) {
                        LOG_DEBUG("Quit event signaled or running set to false. Initiating shutdown sequence...");
                        appState.g_running = false;
                        {
                            std::lock_guard<std::mutex> lock(appState.cv_mtx);
                            appState.exitFlag = true;
                        }
                        appState.cv.notify_one();
                        break;
                    }
                }
            });
        }

        {
            std::unique_lock<std::mutex> lock(appState.cv_mtx);
            appState.cv.wait(lock, [&appState] { return !appState.g_running.load(); });
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
