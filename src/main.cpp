// main.cpp

#include <windows.h>
#include <chrono>
#include <thread>
#include <string>
#include <memory>
#include <stdexcept>
#include <atomic>
#include <csignal>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

// Project-Specific Includes
#include "ConfigParser.h"        // Parsing and validating application configurations
#include "Defconf.h"             // Definitions (e.g., EVENT_NAME and MUTEX_NAME)
#include "VoicemeeterAPI.h"      // Voicemeeter API functions for interacting with Voicemeeter software
#include "VoicemeeterManager.h"  // Manages Voicemeeter instances and configuration
#include "VolumeMirror.h"        // Core mirroring functionality for audio volume levels
#include "DeviceMonitor.h"       // Manages monitoring of device states and events
#include "Logger.h"              // Logging utilities for debugging and information output
#include "VolumeUtils.h"         // Utility functions and definitions for volume controls
#include "cxxopts.hpp"           // Command-line parsing library for handling CLI options

// Aliases for improved code readability
using namespace std::string_view_literals;
using UniqueHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;
using Microsoft::WRL::ComPtr;

// Global Variables
std::atomic<bool> g_running(true);
UniqueHandle g_hQuitEvent = nullptr;
std::mutex cv_mtx;
std::condition_variable cv;
bool exitFlag = false;

// Forward declaration of functions
bool InitializeQuitEvent();
void WaitForQuitEvent();
void signalHandler(int signum);

// Helper function to set Windows volume
bool SetWindowsVolume(int volumePercent) {
    if (volumePercent < 0 || volumePercent > 100) {
        LOG_ERROR("Invalid volume percentage: " + std::to_string(volumePercent));
        return false;
    }

    HRESULT hr;
    ComPtr<IMMDeviceEnumerator> pEnumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), &pEnumerator);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create MMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return false;
    }

    ComPtr<IMMDevice> pDevice;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get default audio endpoint. HRESULT: " + std::to_string(hr));
        return false;
    }

    ComPtr<IAudioEndpointVolume> pEndpointVolume;
    hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, &pEndpointVolume);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to activate IAudioEndpointVolume. HRESULT: " + std::to_string(hr));
        return false;
    }

    float scalar = VolumeUtils::PercentToScalar(static_cast<float>(volumePercent));
    // Set volume
    hr = pEndpointVolume->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set master volume. HRESULT: " + std::to_string(hr));
        return false;
    }

    LOG_INFO("Windows volume set to " + std::to_string(volumePercent) + "%");
    return true;
}

// Helper function to play the startup sound
void PlayStartupSound() {
    // Define the path to the sound file
    const wchar_t* soundFilePath = L"C:\\Windows\\Media\\Windows Unlock.wav"; // Modify if needed
    BOOL played = PlaySoundW(soundFilePath, NULL, SND_FILENAME | SND_ASYNC);
    if (!played) {
        LOG_ERROR("Failed to play startup sound. Attempting to play fallback sound.");
        // Play a fallback sound, e.g., system asterisk
        played = PlaySoundW(L"SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC);
        if (!played) {
            LOG_ERROR("Failed to play fallback startup sound.");
        } else {
            LOG_INFO("Fallback startup sound played successfully.");
        }
    } else {
        LOG_INFO("Startup sound played successfully.");
    }
}

int main(int argc, char *argv[]) {
    // Register signal handler for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::unordered_map<std::string, std::string> configMap;
    std::ofstream logFileStream;

    // Create a mutex to ensure single instance
    UniqueHandle hMutex(CreateMutexA(NULL, FALSE, MUTEX_NAME));
    if (!hMutex) {
        LOG_ERROR("Failed to create mutex.");
        return 1;  // Exit cleanly
    }

    // Initialize quit event
    if (!InitializeQuitEvent()) {
        LOG_ERROR("Unable to initialize quit event.");
        return -1;
    }

    // Check if another instance is running
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        UniqueHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent) {
            SetEvent(hQuitEvent.get());
            LOG_INFO("Signaled existing instance to quit.");
            return 0;
        }
        LOG_ERROR("Failed to signal existing instance.");
        return 1;
    }

    // Create and parse command-line options
    cxxopts::Options options = ConfigParser::CreateOptions();
    // Add new command-line options for startup volume and startup sound
    options.add_options()
        ("startup-volume", "Set the initial Windows volume level as a percentage (0-100)", cxxopts::value<int>())
        ("startup-sound", "Play a startup sound after setting the initial volume", cxxopts::value<bool>()->default_value("false"));

    cxxopts::ParseResult result;
    try {
        options.positional_help("[optional args]")
            .show_positional_help();

        options.allow_unrecognised_options();
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::parsing &e) {
        LOG_ERROR("Unrecognized option or argument error: " + std::string(e.what()));
        LOG_INFO("Use --help to see available options.");
        return -1;
    } catch (const std::exception &e) {
        LOG_ERROR("Error parsing options: " + std::string(e.what()));
        LOG_INFO("Use --help to see available options.");
        return -1;
    }

    Config config;
    try {
        ConfigParser::ValidateOptions(result);
    } catch (const std::exception &e) {
        LOG_ERROR("Validation error: " + std::string(e.what()));
        return -1;
    }

    // Handle configuration file
    if (result.count("config")) {
        config.configFilePath = result["config"].as<std::string>();
    } else {
        config.configFilePath = "VoiceMirror.conf";
    }

    ConfigParser::HandleConfiguration(config.configFilePath, config);
    ConfigParser::ApplyCommandLineOptions(result, config);

    // Set startup volume and startup sound from config file or command-line
    if (result.count("startup-volume")) {
        config.startupVolumePercent = result["startup-volume"].as<int>();
    }

    if (result.count("startup-sound")) {
        config.startupSound = result["startup-sound"].as<bool>();
    }

    // Validate toggle configuration after all options are applied
    try {
        ConfigParser::ValidateToggleConfig(config);
    } catch (const std::exception &e) {
        LOG_ERROR("Toggle configuration validation error: " + std::string(e.what()));
        return -1;
    }

    // Setup logging
    if (!ConfigParser::SetupLogging(config)) {
        return -1;
    }

    // Handle special commands like --help, --version, --shutdown
    if (ConfigParser::HandleSpecialCommands(config)) {
        return 0;
    }

    // Hide console window if specified
    if (config.hideConsole) {
        HWND hWnd = GetConsoleWindow();
        if (hWnd != NULL) {
            if (FreeConsole() == 0) {
                LOG_ERROR("Failed to detach console. Error: " + std::to_string(GetLastError()));
            }
        } else {
            LOG_ERROR("Failed to get console window handle.");
        }
    }

    // Instantiate VoicemeeterManager early to manage COM
    VoicemeeterManager vmrManager;
    vmrManager.SetDebugMode(config.debug);

    if (!vmrManager.Initialize(config.voicemeeterType)) {
        LOG_ERROR("Failed to initialize Voicemeeter Manager.");
        return -1;
    }

    // Now it's safe to list devices as COM is initialized via VoicemeeterManager
    if (config.listMonitor) {
        vmrManager.ListMonitorableDevices();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    if (config.listInputs) {
        vmrManager.ListInputs();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    if (config.listOutputs) {
        vmrManager.ListOutputs();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    if (config.listChannels) {
        vmrManager.ListAllChannels();
        vmrManager.Shutdown();  // Clean up
        return 0;
    }

    // **Set Startup Volume and Play Startup Sound**
    if (config.startupVolumePercent != -1) {
        // Set the startup volume
        if (!SetWindowsVolume(config.startupVolumePercent)) {
            LOG_ERROR("Failed to set startup volume.");
        }

        // Play startup sound after setting the volume
        if (config.startupSound) {
            PlayStartupSound();
        }
    }

    // Proceed to instantiate VolumeMirror
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

    std::unique_ptr<VolumeMirror> mirror = nullptr;
    std::unique_ptr<DeviceMonitor> deviceMonitor = nullptr;
    try {
        // Before creating the VolumeMirror instance, convert the string to ChannelType
        VolumeUtils::ChannelType channelType;
        if (typeStr == "input") {
            channelType = VolumeUtils::ChannelType::Input;
        } else if (typeStr == "output") {
            channelType = VolumeUtils::ChannelType::Output;
        } else {
            LOG_ERROR("Invalid channel type: " + typeStr);
            return -1;
        }

        // Instantiate VolumeMirror with the channel type and playSound flag
        mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, config.sound);
        mirror->SetPollingMode(config.pollingEnabled, config.pollingInterval);
        mirror->Start();
        LOG_INFO("Volume mirroring started.");

        if (isMonitoring) {
            deviceMonitor = std::make_unique<DeviceMonitor>(monitorDeviceUUID, toggleConfig, vmrManager, *mirror);
            LOG_INFO("Started monitoring device UUID: " + monitorDeviceUUID);
        }

        LOG_INFO("VoiceMirror is running. Press Ctrl+C to exit.");

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

        LOG_INFO("VoiceMirror has shut down gracefully.");
    } catch (const std::exception &ex) {
        LOG_ERROR("An error occurred: " + std::string(ex.what()));
        vmrManager.Shutdown();
        return -1;
    }
    return 0;
}

// Implementation of InitializeQuitEvent()
bool InitializeQuitEvent() {
    g_hQuitEvent.reset(CreateEventA(NULL, TRUE, FALSE, EVENT_NAME));
    if (!g_hQuitEvent) {
        LOG_ERROR("Failed to create or open quit event. Error: " + std::to_string(GetLastError()));
        return false;
    }
    LOG_DEBUG("Quit event created or opened successfully.");
    return true;
}

// Signal handler for graceful shutdown
void signalHandler(int signum) {
    LOG_INFO("Interrupt signal (" + std::to_string(signum) + ") received. Shutting down...");
    g_running = false;
    cv.notify_one();
}

// WaitForQuitEvent function runs in a separate thread to wait for the quit event
void WaitForQuitEvent() {
    if (g_hQuitEvent) {
        LOG_DEBUG("Waiting for quit event signal...");
        WaitForSingleObject(g_hQuitEvent.get(), INFINITE);
        LOG_DEBUG("Quit event signaled. Initiating shutdown sequence...");
        g_running = false;  // Set to false to trigger main loop exit

        {
            std::lock_guard<std::mutex> lock(cv_mtx);
            exitFlag = true;
        }
        cv.notify_one();
    } else {
        LOG_ERROR("Quit event handle is null; unable to wait for quit event.");
    }
}
