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
#include "ConfigParser.h"
#include "Defconf.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "VoicemeeterManager.h"
#include "VolumeMirror.h"
#include "VolumeUtils.h"
#include "WindowsManager.h"
#include "cxxopts.hpp"

using namespace std::string_view_literals;
using namespace std::string_literals;

// Application state encapsulation
class Application {
   public:
    Application() : g_running(true), exitFlag(false) {}
    std::atomic<bool> g_running;
    RAIIHandle g_hQuitEvent{nullptr};
    std::mutex cv_mtx;
    std::condition_variable cv;
    bool exitFlag;
};

// Global pointer for signal handler to access Application state
Application* g_appStatePtr = nullptr;

// Initialize quit event with Application reference
bool InitializeQuitEvent(Application& appState) {
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
        if (g_appStatePtr->g_hQuitEvent.get()) {
            SetEvent(g_appStatePtr->g_hQuitEvent.get());
        }
        g_appStatePtr->cv.notify_one();
    }
}

int main(int argc, char* argv[]) {
    Application appState;
    g_appStatePtr = &appState;  // Assign the global pointer for signal handler

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Instantiate ConfigParser with argc and argv
    ConfigParser parser(argc, argv);
    Config appConfig;

    try {
        // Handle configuration parsing
        parser.HandleConfiguration(appConfig);
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // Handle shutdown command if received
    if (appConfig.shutdown.value) {
        LOG_DEBUG("Shutdown command detected.");
        RAIIHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent.get()) {
            if (SetEvent(hQuitEvent.get())) {
                LOG_INFO("Shutdown signal sent to running instances.");
            } else {
                LOG_ERROR("Failed to signal quit event to running instances.");
            }
        } else {
            LOG_INFO("No running instances found.");
        }
        return EXIT_SUCCESS;  // Exit after handling shutdown
    }

    // Single-Instance Enforcement
    RAIIHandle rawMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!rawMutex.get()) {
        LOG_ERROR("Failed to create mutex.");
        return EXIT_FAILURE;  // Exit cleanly
    }

    // Check if another instance is running
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LOG_INFO("Another instance is already running.");
        return EXIT_SUCCESS;  // Exit the second instance without signaling
    }

    // Initialize quit event
    if (!InitializeQuitEvent(appState)) {
        LOG_ERROR("Failed to initialize quit event.");
        return EXIT_FAILURE;
    }

    // Hide console if needed
    if (appConfig.hideConsole.value) {
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (!FreeConsole()) {
                LOG_ERROR("Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            LOG_ERROR("Failed to get console window handle.");
        }
    }

    // Initialize Voicemeeter Manager
    VoicemeeterManager vmrManager;
    vmrManager.SetDebugMode(appConfig.debug.value);

    if (!vmrManager.Initialize(appConfig.voicemeeterType.value)) {
        LOG_ERROR("Failed to initialize and log in to Voicemeeter.");
        Logger::Instance().Shutdown();
        return EXIT_FAILURE;
    }

    if (appConfig.listInputs.value) {
        vmrManager.ListInputs();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_SUCCESS;
    }

    if (appConfig.listOutputs.value) {
        vmrManager.ListOutputs();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_SUCCESS;
    }

    if (appConfig.listChannels.value) {
        vmrManager.ListAllChannels();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_SUCCESS;
    }

    // Create WindowsManager instance
    std::unique_ptr<WindowsManager> windowsManager;
    try {
        windowsManager = std::make_unique<WindowsManager>(appConfig.monitorDeviceUUID.value);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create WindowsManager: " + std::string(e.what()));
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_FAILURE;
    }

    // Apply toggle configuration on startup if monitor device is connected
    windowsManager->onDevicePluggedIn = [&vmrManager, &appConfig]() {
        ToggleConfig toggleConfig;
        try {
            toggleConfig = ConfigParser::ParseToggleParameter(appConfig.toggleParam.value);
        } catch (const std::exception& ex) {
            LOG_ERROR("Exception while parsing toggle parameter on startup: " + std::string(ex.what()));
            return;
        }

        // Unmute index1 and mute index2 based on toggle configuration
        ChannelType channelType = (toggleConfig.type == "input") ? ChannelType::Input : ChannelType::Output;
        vmrManager.SetMute(toggleConfig.index1, channelType, false);
        vmrManager.SetMute(toggleConfig.index2, channelType, true);

        LOG_INFO("Applied toggle settings on startup: " + toggleConfig.type +
                 " channel " + std::to_string(toggleConfig.index1) + " unmuted, " +
                 "channel " + std::to_string(toggleConfig.index2) + " muted.");
    };

    // Call CheckDevice directly to see if device is already connected on startup
    if (!appConfig.monitorDeviceUUID.value.empty()) {
        windowsManager->CheckDevice(std::wstring(appConfig.monitorDeviceUUID.value.begin(), appConfig.monitorDeviceUUID.value.end()).c_str(), true);
    }

    // Handle listing of monitorable devices
    if (appConfig.listMonitor.value) {
        windowsManager->ListMonitorableDevices();
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_SUCCESS;
    }

    // Set device plug/unplug callbacks
    ToggleConfig toggleConfig;
    if (!appConfig.toggleParam.value.empty()) {
        try {
            toggleConfig = parser.ParseToggleParameter(appConfig.toggleParam.value);
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("Exception while parsing toggle parameter: ") + ex.what());
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return EXIT_FAILURE;
        }
    }

    windowsManager->onDevicePluggedIn = [&vmrManager, &toggleConfig]() {
        std::lock_guard<std::mutex> lock(vmrManager.toggleMutex);
        vmrManager.RestartAudioEngine();
        if (!toggleConfig.type.empty()) {
            ChannelType channelType = toggleConfig.type == "input" ? ChannelType::Input : ChannelType::Output;
            vmrManager.SetMute(toggleConfig.index1, channelType, false);
            vmrManager.SetMute(toggleConfig.index2, channelType, true);
        }
    };

    windowsManager->onDeviceUnplugged = [&vmrManager, &toggleConfig]() {
        std::lock_guard<std::mutex> lock(vmrManager.toggleMutex);
        if (!toggleConfig.type.empty()) {
            ChannelType channelType = toggleConfig.type == "input" ? ChannelType::Input : ChannelType::Output;
            vmrManager.SetMute(toggleConfig.index1, channelType, true);
            vmrManager.SetMute(toggleConfig.index2, channelType, false);
        }
    };

    int channelIndex = appConfig.index.value;
    const std::string& typeStr = appConfig.type.value;
    float minDbm = appConfig.minDbm.value;
    float maxDbm = appConfig.maxDbm.value;

    const std::string& monitorDeviceUUID = appConfig.monitorDeviceUUID.value;
    const bool isMonitoring = !monitorDeviceUUID.empty();

    std::unique_ptr<VolumeMirror> mirror = nullptr;
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
            return EXIT_FAILURE;
        }

        mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, *windowsManager, appConfig.chime.value);

        mirror->SetPollingMode(appConfig.pollingEnabled.value, appConfig.pollingInterval.value);
        mirror->Start();
        LOG_INFO("Volume mirroring started.");

        LOG_INFO("VoiceMirror is running. Press Ctrl+C to exit.");

        std::thread quitThread;

        // Set startup volume if different from default
        if (appConfig.startupVolumePercent.value != DEFAULT_STARTUP_VOLUME_PERCENT) {
            LOG_DEBUG("Setting startup volume to " + std::to_string(appConfig.startupVolumePercent.value) + "%");

            // Use a try-catch block for the volume setting operation
            try {
                if (windowsManager->SetVolume(static_cast<float>(appConfig.startupVolumePercent.value))) {
                    LOG_DEBUG("Startup volume set successfully.");
                } else {
                    LOG_ERROR("Failed to set startup volume.");
                }
            } catch (const std::exception& ex) {
                LOG_ERROR("Volume setting failed: " + std::string(ex.what()));
            }
        }
        // Handle startup sound in a separate thread if enabled
        if (appConfig.startupSound.value) {
            std::thread startupSoundThread(&WindowsManager::PlayStartupSound, windowsManager.get());
            startupSoundThread.detach();
        }

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

        mirror.reset();
        windowsManager.reset();  // Release WindowsManager resources
        vmrManager.Shutdown();
        LOG_INFO("VoiceMirror has shut down gracefully.");

        Logger::Instance().Shutdown();

        if (quitThread.joinable()) {
            quitThread.join();
        }

    } catch (const std::exception& ex) {
        LOG_ERROR("An error occurred: " + std::string(ex.what()));
        if (mirror) {
            mirror->Stop();
        }
        mirror.reset();
        windowsManager.reset();  // Ensure cleanup in case of exceptions during WindowsManager creation
        vmrManager.Shutdown();
        Logger::Instance().Shutdown();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
