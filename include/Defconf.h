// Defconf.h
#pragma once

#include <string>

// -----------------------------
// Mutex and Event Names
// -----------------------------

constexpr char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";          // Mutex to prevent multiple instances
constexpr char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";      // Event to signal application shutdown

// -----------------------------
// Default Paths
// -----------------------------

// Default paths to Voicemeeter DLLs based on system architecture
constexpr const char* DEFAULT_DLL_PATH_64 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote64.dll";
constexpr const char* DEFAULT_DLL_PATH_32 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote.dll";

// Configuration file and logging paths
constexpr const char* DEFAULT_CONFIG_FILE = "VoiceMirror.conf";      // Default configuration file
constexpr const char* DEFAULT_LOG_FILE = "VoiceMirror.log";          // Default log file

// -----------------------------
// Voicemeeter Settings and Configuration Defaults
// -----------------------------

constexpr int DEFAULT_CHANNEL_INDEX = 3;                           // Default channel index for audio routing
constexpr int DEFAULT_VOICEMEETER_TYPE = 2;                        // Default Voicemeeter type (2 = Banana)
constexpr int DEFAULT_POLLING_INTERVAL_MS = 100;                   // Default polling interval for status checks

// Retry settings for API initialization
constexpr int MAX_RETRIES = 20;                                     // Maximum number of connection attempts
constexpr int RETRY_DELAY_MS = 1000;                                // Delay between connection attempts (milliseconds)

// -----------------------------
// Chime Settings
// -----------------------------

constexpr const wchar_t* SYNC_SOUND_FILE_PATH = L"C:\\Windows\\Media\\Windows Unlock.wav"; // Path to synchronization sound
constexpr const wchar_t* SYNC_FALLBACK_SOUND_ALIAS = L"SystemAsterisk";                      // Fallback sound alias

// Debounce duration in milliseconds
constexpr int DEBOUNCE_DURATION_MS = 50;

// -----------------------------
// Audio Level Boundaries and Defaults
// -----------------------------

constexpr float DEFAULT_MIN_DBM = -60.0f;                          // Minimum decibel level
constexpr float DEFAULT_MAX_DBM = 12.0f;                           // Maximum decibel level

// Volume settings
constexpr int DEFAULT_STARTUP_VOLUME_PERCENT = -1;                  // Default volume at startup (-1 = unchanged)
constexpr int DEFAULT_MAX_VOLUME_PERCENT = 100;                     // Maximum allowed volume percentage
constexpr int DEFAULT_MIN_VOLUME_PERCENT = 0;                       // Minimum allowed volume percentage

// -----------------------------
// Toggle Parameters
// -----------------------------

constexpr const char* DEFAULT_TOGGLE_PARAM = "input:0:1";           // Default toggle parameter format
constexpr const char* DEFAULT_TOGGLE_COMMAND = "";                  // Default toggle command (empty means not set)

// -----------------------------
// Application Behavior Defaults
// -----------------------------

constexpr bool DEFAULT_DEBUG_MODE = false;                          // Default debug mode status
constexpr bool DEFAULT_HIDDEN_CONSOLE = false;                      // Default console visibility
constexpr bool DEFAULT_LOGGING_ENABLED = false;                     // Default logging status
constexpr bool DEFAULT_CHIME_ENABLED = false;                       // Default chime sound status
constexpr bool DEFAULT_POLLING_ENABLED = false;                     // Default polling mode status
constexpr bool DEFAULT_SHUTDOWN_ENABLED = false;                    // Default shutdown command status
constexpr bool DEFAULT_STARTUP_SOUND_ENABLED = false;               // Default startup sound status
constexpr bool DEFAULT_HELP_FLAG = false;                           // Default help flag status
constexpr bool DEFAULT_VERSION_FLAG = false;                        // Default version flag status

// -----------------------------
// Command-Line Option Defaults
// -----------------------------

constexpr const char* DEFAULT_TYPE = "input";                        // Default channel type ("input" or "output")
constexpr const char* DEFAULT_MONITOR_DEVICE_UUID = "";              // Default monitor device UUID (empty means not set)

// -----------------------------
// Version Information
// -----------------------------

constexpr int VERSION_MAJOR = 0;                                     // Major version number
constexpr int VERSION_MINOR = 2;                                     // Minor version number
constexpr int VERSION_PATCH = 0;                                     // Patch version number
constexpr const char* VERSION_PRE_RELEASE = "alpha";                 // Pre-release tag (e.g., "alpha", "beta", "rc")
// If there's no pre-release tag, set to an empty string:
// constexpr const char* VERSION_PRE_RELEASE = "";

// -----------------------------
// Voicemeeter Type Enumeration
// -----------------------------

enum VoicemeeterType {
    VOICEMEETER_BASIC = 1,
    VOICEMEETER_BANANA,
    VOICEMEETER_POTATO
};

// -----------------------------
// Channel Type Enumeration
// -----------------------------

enum class ChannelType {
    Input,
    Output
};

// -----------------------------
// Configuration Structure
// -----------------------------

struct Config {
    // File Paths
    std::string configFilePath = DEFAULT_CONFIG_FILE;               // Configuration file location
    std::string logFilePath = DEFAULT_LOG_FILE;                     // Log file location

    // Debugging and Logging
    bool debug = DEFAULT_DEBUG_MODE;                                 // Enable debug logging
    bool loggingEnabled = DEFAULT_LOGGING_ENABLED;                  // Enable file logging

    // Application Behavior
    bool help = DEFAULT_HELP_FLAG;                                   // Show help information
    bool version = DEFAULT_VERSION_FLAG;                             // Show version information
    bool hideConsole = DEFAULT_HIDDEN_CONSOLE;                       // Hide console window
    bool shutdown = DEFAULT_SHUTDOWN_ENABLED;                        // Trigger application shutdown
    bool chime = DEFAULT_CHIME_ENABLED;                              // Play notification sounds
    bool pollingEnabled = DEFAULT_POLLING_ENABLED;                   // Enable status polling
    bool startupSound = DEFAULT_STARTUP_SOUND_ENABLED;               // Play sound at startup

    // Volume Settings
    int startupVolumePercent = DEFAULT_STARTUP_VOLUME_PERCENT;       // Initial volume level
    int maxVolumePercent = DEFAULT_MAX_VOLUME_PERCENT;               // Maximum allowed volume level
    int minVolumePercent = DEFAULT_MIN_VOLUME_PERCENT;               // Minimum allowed volume level

    // Voicemeeter Settings
    int voicemeeterType = DEFAULT_VOICEMEETER_TYPE;                 // Voicemeeter variant selection (1: Basic, 2: Banana, 3: Potato)
    int index = DEFAULT_CHANNEL_INDEX;                               // Channel index for operations

    // Audio Levels
    float maxDbm = DEFAULT_MAX_DBM;                                  // Maximum audio level
    float minDbm = DEFAULT_MIN_DBM;                                  // Minimum audio level

    // Device and Toggle Settings
    std::string monitorDeviceUUID = DEFAULT_MONITOR_DEVICE_UUID;     // Audio monitoring device ID
    std::string toggleParam = DEFAULT_TOGGLE_PARAM;                  // Parameter to toggle
    std::string toggleCommand = DEFAULT_TOGGLE_COMMAND;              // Toggle command parameter

    // Polling Settings
    int pollingInterval = DEFAULT_POLLING_INTERVAL_MS;               // Status polling frequency (milliseconds)

    // Channel Type
    std::string type = DEFAULT_TYPE;                                 // Channel type selection ("input" or "output")

    // Listing Flags
    bool listMonitor = false;                                        // Flag to list monitor devices
    bool listInputs = false;                                         // Flag to list Voicemeeter inputs
    bool listOutputs = false;                                        // Flag to list Voicemeeter outputs
    bool listChannels = false;                                       // Flag to list Voicemeeter channels
};
