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

#include "ConfigParser.h"
#include "Defconf.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "SoundManager.h"
#include "VoicemeeterManager.h"
#include "VolumeMirror.h"
#include "VolumeUtils.h"
#include "WindowsManager.h"
#include "cxxopts.hpp"

using namespace std::string_view_literals;
using namespace std::string_literals;

// Forward declaration of Application to be used in the control handler
class Application;

// Global pointer to the application state
Application* g_appStatePtr = nullptr;

// Application class definition
class Application {
   public:
    Application() : g_running(true), exitFlag(false) {}
    std::atomic<bool> g_running;
    RAIIHandle g_hQuitEvent{nullptr};
    std::mutex cv_mtx;
    std::condition_variable cv;
    bool exitFlag;
};

// Control Handler Function for Console Events
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_LOGOFF_EVENT ||
        dwCtrlType == CTRL_SHUTDOWN_EVENT) {
        if (g_appStatePtr) {
            // Perform minimal, thread-safe operations
            g_appStatePtr->g_running = false;

            if (g_appStatePtr->g_hQuitEvent.get()) {
                SetEvent(g_appStatePtr->g_hQuitEvent.get());
            }

            // Notify the condition variable
            g_appStatePtr->cv.notify_one();
        }

        // Return TRUE to indicate that the event has been handled
        return TRUE;
    }
    return FALSE;
}

// Function to Initialize Quit Event
bool InitializeQuitEvent(Application& appState) {
    appState.g_hQuitEvent = RAIIHandle(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!appState.g_hQuitEvent.get()) {
        LOG_ERROR("[InitializeQuitEvent] Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return false;
    }
    LOG_DEBUG("[InitializeQuitEvent] Quit event created or opened successfully.");
    return true;
}

int main(int argc, char* argv[]) {
    Application appState;
    g_appStatePtr = &appState;

    // Register the Windows control handler
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        LOG_ERROR("[main] Failed to set control handler. Error: " + std::to_string(GetLastError()));
        return EXIT_FAILURE;
    }

    ConfigParser parser(argc, argv);
    Config appConfig;

    try {
        parser.HandleConfiguration(appConfig);
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    const Config& config = appConfig;

    if (appConfig.shutdown.value) {
        LOG_DEBUG("[main] Shutdown command detected.");
        RAIIHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent.get()) {
            if (SetEvent(hQuitEvent.get())) {
                LOG_INFO("[main] Shutdown signal sent to running instances.");
            } else {
                LOG_ERROR("[main] Failed to signal quit event to running instances.");
            }
        } else {
            LOG_INFO("[main] No running instances found.");
        }
        return EXIT_SUCCESS;
    }

    RAIIHandle quitEventHandle(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!quitEventHandle.get()) {
        LOG_ERROR("[main] Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return EXIT_FAILURE;
    }

    RAIIHandle mutexHandle(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!mutexHandle.get()) {
        LOG_ERROR("[main] Failed to create mutex.");
        return EXIT_FAILURE;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LOG_INFO("[main] Another instance is already running.");
        return EXIT_SUCCESS;
    }

    if (!InitializeQuitEvent(appState)) {
        LOG_ERROR("[main] Failed to initialize quit event.");
        return EXIT_FAILURE;
    }

    if (appConfig.hideConsole.value) {
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (!FreeConsole()) {
                LOG_ERROR("[main] Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            LOG_ERROR("[main] Failed to get console window handle.");
        }
    }

    SoundManager::Instance().Initialize(
        VolumeUtils::ConvertToWString(appConfig.startupSoundFilePath.value),
        VolumeUtils::ConvertToWString(appConfig.syncSoundFilePath.value));

    std::unique_ptr<WindowsManager> windowsManager;
    try {
        windowsManager = std::make_unique<WindowsManager>(appConfig);
    } catch (const std::exception& e) {
        LOG_ERROR("[main] Failed to create WindowsManager: " + std::string(e.what()));
        Logger::Instance().Shutdown();
        return EXIT_FAILURE;
    }

    VoicemeeterManager vmrManager;

    if (!vmrManager.Initialize(appConfig.voicemeeterType.value)) {
        LOG_ERROR("[main] Failed to initialize and log in to Voicemeeter.");
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

    if (!appConfig.toggleParam.value.empty()) {
        ToggleConfig toggleConfig;
        try {
            toggleConfig = ConfigParser::ParseToggleParameter(appConfig.toggleParam.value);
        } catch (const std::exception& ex) {
            LOG_ERROR("[main] Exception while parsing toggle parameter on startup: " + std::string(ex.what()));
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return EXIT_FAILURE;
        }

        windowsManager->onDevicePluggedIn = [&vmrManager, &toggleConfig]() {
            ChannelType channelType = (std::string(toggleConfig.type) == "input")
                                          ? ChannelType::Input
                                          : ChannelType::Output;
            vmrManager.SetMute(toggleConfig.index1, channelType, false);
            vmrManager.SetMute(toggleConfig.index2, channelType, true);
            LOG_INFO("[main] Applied toggle settings on startup: type=" +
                     std::to_string(static_cast<int>(toggleConfig.index1)) +
                     " unmuted, channel " +
                     std::to_string(static_cast<int>(toggleConfig.index2)) +
                     " muted.");
        };

        if (appConfig.listMonitor.value) {
            windowsManager->ListMonitorableDevices();
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return EXIT_SUCCESS;
        }

        // Assign callbacks without using toggleMutex
        windowsManager->onDevicePluggedIn = [&vmrManager, &toggleConfig]() {
            ChannelType channelType = (std::string(toggleConfig.type) == "input")
                                          ? ChannelType::Input
                                          : ChannelType::Output;
            vmrManager.SetMute(toggleConfig.index1, channelType, false);
            vmrManager.SetMute(toggleConfig.index2, channelType, true);
        };

        windowsManager->onDeviceUnplugged = [&vmrManager, &toggleConfig]() {
            ChannelType channelType = (std::string(toggleConfig.type) == "input")
                                          ? ChannelType::Input
                                          : ChannelType::Output;
            vmrManager.SetMute(toggleConfig.index1, channelType, true);
            vmrManager.SetMute(toggleConfig.index2, channelType, false);
        };

        uint8_t channelIndex = appConfig.index.value;
        ChannelType channelType = (std::string(appConfig.type.value) == "input")
                                      ? ChannelType::Input
                                      : ChannelType::Output;
        int8_t minDbm = appConfig.minDbm.value;
        int8_t maxDbm = appConfig.maxDbm.value;

        std::string_view monitorDeviceUUID = appConfig.monitorDeviceUUID.value;
        bool isMonitoring = (!monitorDeviceUUID.empty());

        std::unique_ptr<VolumeMirror> mirror = nullptr;
        try {
            VolumeMirror::Mode mirrorMode = VolumeMirror::Mode::Callback;

            if (appConfig.pollingEnabled.value) {
                mirrorMode = VolumeMirror::Mode::Polling;
            } /* else if (appConfig.hybridMode.value) {
                 mirrorMode = VolumeMirror::Mode::Hybrid;
             }*/
            mirrorMode = VolumeMirror::Mode::Hybrid;
            VolumeMirror& mirror = VolumeMirror::Instance(
                channelIndex,
                channelType,
                vmrManager,
                *windowsManager,
                mirrorMode);

            mirror.Start();
            LOG_INFO("[main] Volume mirroring started.");

            LOG_INFO("[main] VoiceMirror is running. Press Ctrl+C to exit.");

            std::thread quitThread;

            if (appConfig.startupVolumePercent.value != DEFAULT_STARTUP_VOLUME_PERCENT) {
                LOG_DEBUG("[main] Setting startup volume to " + std::to_string(appConfig.startupVolumePercent.value) + "%");

                try {
                    if (windowsManager->SetVolume(static_cast<float>(appConfig.startupVolumePercent.value))) {
                        LOG_DEBUG("[main] Startup volume set successfully.");
                    } else {
                        LOG_ERROR("[main] Failed to set startup volume.");
                    }
                } catch (const std::exception& ex) {
                    LOG_ERROR("[main] Volume setting failed: " + std::string(ex.what()));
                }
            }

            if (appConfig.startupSound.value) {
                std::wstring startupSoundPath = VolumeUtils::ConvertToWString(appConfig.startupSoundFilePath.value);
                SoundManager::Instance().PlayStartupSound();
            }

            if (isMonitoring) {
                quitThread = std::thread([&appState]() {
                    while (appState.g_running.load()) {
                        DWORD result = WaitForSingleObject(appState.g_hQuitEvent.get(), 500);
                        if (result == WAIT_OBJECT_0 || !appState.g_running.load()) {
                            LOG_DEBUG("[main] Quit event signaled or running set to false. Initiating shutdown sequence...");
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

            mirror.Stop();
            windowsManager.reset();
            vmrManager.Shutdown();
            LOG_INFO("[main] VoiceMirror has shut down gracefully.");

            Logger::Instance().Shutdown();

            if (quitThread.joinable()) {
                quitThread.join();
            }

        } catch (const std::exception& ex) {
            LOG_ERROR("[main] An error occurred: " + std::string(ex.what()));

            // mirror.Stop();
            windowsManager.reset();
            vmrManager.Shutdown();
            Logger::Instance().Shutdown();
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }
}
