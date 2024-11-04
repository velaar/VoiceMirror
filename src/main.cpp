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
#include "Defconf.h"               // Definitions and configuration constants
#include "Logger.h"                // Logging utilities for debugging and information output
#include "RAIIHandle.h"            // RAII wrapper for HANDLEs
#include "VoicemeeterManager.h"    // Manages Voicemeeter instances and configuration
#include "VolumeMirror.h"          // Core mirroring functionality for audio volume levels
#include "VolumeUtils.h"           // Utility functions and definitions for volume controls
#include "WindowsManager.h"  // Windows-specific volume management
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
    RAIIHandle g_hQuitEvent{ nullptr };
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

void PlayStartupSound() {
    // Introduce a slight delay (e.g., 500 milliseconds) before playing the sound
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Define the path to the startup sound file
    // Ensure that "m95.mp3" is located in the same directory as the executable
    const std::wstring soundFilePath = L"m95.mp3";

    // Define an alias for the media
    constexpr const wchar_t* aliasName = L"StartupSound";

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
        }
        else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to open audio file with MCI. Error: ") + errorStr);
        return;
    }

    // Send the play command
    std::wstring playCommand = std::wstring(L"play ") + aliasName + L" wait";
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
        }
        else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to play audio file with MCI. Error: ") + errorStr);

        // Attempt to close the alias before returning
        std::wstring closeCommand = std::wstring(L"close ") + aliasName;
        mciSendStringW(closeCommand.c_str(), NULL, 0, NULL);
        return;
    }

    // Send the close command to release resources
    std::wstring closeCommand = std::wstring(L"close ") + aliasName;
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
        }
        else {
            // Fallback if conversion fails
            errorStr = "Unknown error";
        }

        LOG_ERROR(std::string("Failed to close audio file with MCI. Error: ") + errorStr);
        return;
    }

    LOG_DEBUG("Startup sound played successfully.");
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
          // Handle configuration parsing (includes parsing config file and command-line options)
          parser.HandleConfiguration(appConfig.configFilePath, appConfig);
      }
      catch (const std::exception& e) {
          std::cerr << "Configuration error: " << e.what() << std::endl;
          return EXIT_FAILURE;
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
          // Optionally, you can bring the existing instance to the foreground here
          return EXIT_SUCCESS;  // Exit the second instance without signaling
      }

      // Initialize quit event
      if (!InitializeQuitEvent(appState)) {
          LOG_ERROR("Failed to initialize quit event.");
          return EXIT_FAILURE;
      }

      // Hide console if needed
      if (appConfig.hideConsole) {
          HWND hWnd = GetConsoleWindow();
          if (hWnd != NULL) {
              if (!FreeConsole()) {
                  LOG_ERROR("Failed to detach console. Error: " + std::to_string(GetLastError()));
              }
          }
          else {
              LOG_ERROR("Failed to get console window handle.");
          }
      }

      // Initialize Logger
      try {
          // Logger should have been initialized in ConfigParser.HandleConfiguration
          // So no need to initialize here again
      }
      catch (const std::exception& e) {
          std::cerr << "Logger initialization failed: " << e.what() << std::endl;
          return EXIT_FAILURE;
      }

      // Initialize Voicemeeter Manager
      VoicemeeterManager vmrManager;
      vmrManager.SetDebugMode(appConfig.debug);

      if (!vmrManager.Initialize(appConfig.voicemeeterType)) {
          LOG_ERROR("Failed to initialize Voicemeeter Manager.");
          Logger::Instance().Shutdown();
          return EXIT_FAILURE;
      }

      // Handle listing of monitors, inputs, outputs, and channels based on configuration
      if (appConfig.listMonitor) {
          vmrManager.ListMonitorableDevices();
          vmrManager.Shutdown();
          Logger::Instance().Shutdown();
          return EXIT_SUCCESS;
      }

      if (appConfig.listInputs) {
          vmrManager.ListInputs();
          vmrManager.Shutdown();
          Logger::Instance().Shutdown();
          return EXIT_SUCCESS;
      }

      if (appConfig.listOutputs) {
          vmrManager.ListOutputs();
          vmrManager.Shutdown();
          Logger::Instance().Shutdown();
          return EXIT_SUCCESS;
      }

      if (appConfig.listChannels) {
          vmrManager.ListAllChannels();
          vmrManager.Shutdown();
          Logger::Instance().Shutdown();
          return EXIT_SUCCESS;
      }

      // Create a scoped WindowsManager instance
      std::unique_ptr<WindowsManager> windowsManager = nullptr; // Use a smart pointer
      try {
          // Declare toggleConfig HERE, before using it in WindowsManager constructor
          ToggleConfig toggleConfig;  // Corrected line: Declaration added

          if (!appConfig.toggleParam.empty()) {
              try {
                  toggleConfig = parser.ParseToggleParameter(appConfig.toggleParam);
              }
              catch (const std::exception& ex) {
                  LOG_ERROR(std::string("Exception while parsing toggle parameter: ") + ex.what());
                  vmrManager.Shutdown();
                  Logger::Instance().Shutdown();
                  return EXIT_FAILURE;
              }
          }

          windowsManager = std::make_unique<WindowsManager>(appConfig.monitorDeviceUUID, toggleConfig, vmrManager);


          // Set startup volume if different from default
          if (appConfig.startupVolumePercent != DEFAULT_STARTUP_VOLUME_PERCENT) {
              LOG_DEBUG("Setting startup volume to " + std::to_string(appConfig.startupVolumePercent) + "%");
            
              // Use a try-catch block for the volume setting operation
              try {
                  if (windowsManager->SetVolume(static_cast<float>(appConfig.startupVolumePercent))) {
                      LOG_DEBUG("Startup volume set successfully.");
                    
                      // Handle startup sound in a separate thread if enabled
                      if (appConfig.startupSound) {
                          std::thread startupSoundThread(PlayStartupSound);
                          startupSoundThread.detach();
                      }
                  } else {
                      LOG_ERROR("Failed to set startup volume.");
                  }
              } catch (const std::exception& ex) {
                  LOG_ERROR("Volume setting failed: " + std::string(ex.what()));
              }
          }

          int channelIndex = appConfig.index;
          const std::string& typeStr = appConfig.type;
          float minDbm = appConfig.minDbm;
          float maxDbm = appConfig.maxDbm;

          const std::string& monitorDeviceUUID = appConfig.monitorDeviceUUID;
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

              mirror = std::make_unique<VolumeMirror>(channelIndex, channelType, minDbm, maxDbm, vmrManager, appConfig.chime);
              mirror->SetPollingMode(appConfig.pollingEnabled, appConfig.pollingInterval);
              mirror->Start();
              LOG_INFO("Volume mirroring started.");

              if (windowsManager) { // Check if windowsManager was created successfully
                  windowsManager->RegisterVolumeChangeCallback([mirror = mirror.get()](float newVolume, bool isMuted) {
                      if (mirror) {
                          mirror->OnWindowsVolumeChange(newVolume, isMuted);
                      }
                  });
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

              mirror.reset();
              windowsManager.reset(); // Release WindowsManager resources
              vmrManager.Shutdown();
              LOG_INFO("VoiceMirror has shut down gracefully.");

              Logger::Instance().Shutdown();

              if (quitThread.joinable()) {
                  quitThread.join();
              }

          }
          catch (const std::exception& ex) {
              LOG_ERROR("An error occurred: " + std::string(ex.what()));
              if (mirror) {
                  mirror->Stop();
              }
              mirror.reset();
              windowsManager.reset(); // Ensure cleanup in case of exceptions during WindowsManager creation
              vmrManager.Shutdown();
              Logger::Instance().Shutdown();
              return EXIT_FAILURE;
          }
      }
      catch (const std::exception& ex) {
          LOG_ERROR("An error occurred: " + std::string(ex.what()));
          windowsManager.reset(); // Ensure cleanup in case of exceptions during WindowsManager creation
          vmrManager.Shutdown();
          Logger::Instance().Shutdown();
          return EXIT_FAILURE;
      }

      return EXIT_SUCCESS;
  }  // VoicemeeterManager.cpp
