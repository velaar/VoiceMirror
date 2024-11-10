// Defconf.h

#pragma once

#include <string>

// -----------------------------
// Mutex and Event Names
// -----------------------------

constexpr char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";      // Mutex to prevent multiple instances
constexpr char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";  // Event to signal application shutdown
constexpr char COM_INIT_MUTEX_NAME[] = "Global\\VoiceMirrorCOMInitMutex"; // Mutex to shield COM initialization
// -----------------------------
// Default Paths
// -----------------------------

// Default paths to Voicemeeter DLLs based on system architecture
constexpr const char* DEFAULT_DLL_PATH_64 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote64.dll";
constexpr const char* DEFAULT_DLL_PATH_32 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote.dll";

// Configuration file and logging paths
constexpr const char* DEFAULT_CONFIG_FILE = "VoiceMirror.conf";  // Default configuration file
constexpr const char* DEFAULT_LOG_FILE = "VoiceMirror.log";      // Default log file

// -----------------------------
// Voicemeeter Settings and Configuration Defaults
// -----------------------------

constexpr int DEFAULT_CHANNEL_INDEX = 3;          // Default channel index for audio routing
constexpr int DEFAULT_VOICEMEETER_TYPE = 2;       // Default Voicemeeter type (2 = Banana)
constexpr int DEFAULT_POLLING_INTERVAL_MS = 100;  // Default polling interval for status checks
constexpr int DEBOUNCE_DURATION_MS = 50;          // Debounce duration in milliseconds

// Retry settings for API initialization
constexpr int MAX_RETRIES = 20;       // Maximum number of connection attempts
constexpr int RETRY_DELAY_MS = 1000;  // Delay between connection attempts (milliseconds)

// -----------------------------
// Chime Settings
// -----------------------------

constexpr const wchar_t* SYNC_SOUND_FILE_PATH = L"C:\\Windows\\Media\\Windows Unlock.wav";  // Path to synchronization sound
constexpr const wchar_t* SYNC_FALLBACK_SOUND_ALIAS = L"SystemAsterisk";                     // Fallback sound alias

// -----------------------------
// Audio Level Boundaries and Defaults
// -----------------------------

constexpr float DEFAULT_MIN_DBM = -60.0f;  // Minimum decibel level
constexpr float DEFAULT_MAX_DBM = 12.0f;   // Maximum decibel level

// Volume settings
constexpr int DEFAULT_STARTUP_VOLUME_PERCENT = -1;  // Default volume at startup (-1 = unchanged)

// -----------------------------
// Toggle Parameters
// -----------------------------

constexpr const char* DEFAULT_TOGGLE_PARAM = "input:0:1";  // Default toggle parameter format
constexpr const char* DEFAULT_TOGGLE_COMMAND = "";         // Default toggle command (empty means not set)

// -----------------------------
// Application Behavior Defaults
// -----------------------------

constexpr bool DEFAULT_DEBUG_MODE = 
#ifdef _DEBUG
    true
#else
    false
#endif
;                          // Default debug mode status

constexpr bool DEFAULT_HIDDEN_CONSOLE = false;                      // Default console visibility
constexpr bool DEFAULT_LOGGING_ENABLED = false;                     // Default logging status
constexpr bool DEFAULT_CHIME_ENABLED = false;          // Default chime sound status
constexpr bool DEFAULT_POLLING_ENABLED = false;        // Default polling mode status
constexpr bool DEFAULT_SHUTDOWN_ENABLED = false;       // Default shutdown command status
constexpr bool DEFAULT_STARTUP_SOUND_ENABLED = false;  // Default startup sound status
constexpr bool DEFAULT_HELP_FLAG = false;              // Default help flag status
constexpr bool DEFAULT_VERSION_FLAG = false;           // Default version flag status

// -----------------------------
// Command-Line Option Defaults
// -----------------------------

constexpr const char* DEFAULT_TYPE = "input";            // Default channel type ("input" or "output")
constexpr const char* DEFAULT_MONITOR_DEVICE_UUID = "";  // Default monitor device UUID (empty means not set)

// -----------------------------
// Version Information
// -----------------------------

constexpr int VERSION_MAJOR = 0;                      // Major version number
constexpr int VERSION_MINOR = 2;                      // Minor version number
constexpr int VERSION_PATCH = 0;                      // Patch version number
constexpr const char* VERSION_PRE_RELEASE = "alpha";  // Pre-release tag (e.g., "alpha", "beta", "rc")
// If there's no pre-release tag, set to an empty string:
// constexpr const char* VERSION_PRE_RELEASE = "";

// -----------------------------
// Voicemeeter Type Enumeration
// -----------------------------

enum VoicemeeterType {
    VOICEMEETER_BASIC = 1,
    VOICEMEETER_BANANA,
    VOICEMEETER_POTATO,
    VOICEMEETER_BASIC_X64,
    VOICEMEETER_BANANA_X64,
    VOICEMEETER_POTATO_X64,
};

// -----------------------------
// Channel Type Enumeration
// -----------------------------

enum class ChannelType {
    Input,
    Output
};

/**
 * @brief Structure to hold toggle configuration parameters.
 */
struct ToggleConfig {
    std::string type; ///< Type of channel ('input' or 'output').
    int index1;       ///< First channel index.
    int index2;       ///< Second channel index.
};

enum class ConfigSource {
    Default,
    ConfigFile,
    CommandLine
};

template <typename T>
struct ConfigOption {
    T value;
    ConfigSource source = ConfigSource::Default;
};

// -----------------------------
// Configuration Structure
// -----------------------------
struct Config {
    // File Paths
    ConfigOption<std::string> configFilePath = {DEFAULT_CONFIG_FILE, ConfigSource::Default};
    ConfigOption<std::string> logFilePath = {DEFAULT_LOG_FILE, ConfigSource::Default};

    // Debugging and Logging
    ConfigOption<bool> debug = {DEFAULT_DEBUG_MODE, ConfigSource::Default};
    ConfigOption<bool> loggingEnabled = {DEFAULT_LOGGING_ENABLED, ConfigSource::Default};

    // Application Behavior
    ConfigOption<bool> help = {DEFAULT_HELP_FLAG, ConfigSource::Default};
    ConfigOption<bool> version = {DEFAULT_VERSION_FLAG, ConfigSource::Default};
    ConfigOption<bool> hideConsole = {DEFAULT_HIDDEN_CONSOLE, ConfigSource::Default};
    ConfigOption<bool> shutdown = {DEFAULT_SHUTDOWN_ENABLED, ConfigSource::Default};
    ConfigOption<bool> chime = {DEFAULT_CHIME_ENABLED, ConfigSource::Default};
    ConfigOption<bool> pollingEnabled = {DEFAULT_POLLING_ENABLED, ConfigSource::Default};
    ConfigOption<bool> startupSound = {DEFAULT_STARTUP_SOUND_ENABLED, ConfigSource::Default};

    // Volume Settings
    ConfigOption<int> startupVolumePercent = {DEFAULT_STARTUP_VOLUME_PERCENT, ConfigSource::Default};

    // Voicemeeter Settings
    ConfigOption<int> voicemeeterType = {DEFAULT_VOICEMEETER_TYPE, ConfigSource::Default};
    ConfigOption<int> index = {DEFAULT_CHANNEL_INDEX, ConfigSource::Default};

    // Audio Levels
    ConfigOption<float> maxDbm = {DEFAULT_MAX_DBM, ConfigSource::Default};
    ConfigOption<float> minDbm = {DEFAULT_MIN_DBM, ConfigSource::Default};

    // Device and Toggle Settings
    ConfigOption<std::string> monitorDeviceUUID = {DEFAULT_MONITOR_DEVICE_UUID, ConfigSource::Default};
    ConfigOption<std::string> toggleParam = {DEFAULT_TOGGLE_PARAM, ConfigSource::Default};
    ConfigOption<std::string> toggleCommand = {DEFAULT_TOGGLE_COMMAND, ConfigSource::Default};

    // Polling Settings
    ConfigOption<int> pollingInterval = {DEFAULT_POLLING_INTERVAL_MS, ConfigSource::Default};

    // Channel Type
    ConfigOption<std::string> type = {DEFAULT_TYPE, ConfigSource::Default};

    // Listing Flags
    ConfigOption<bool> listMonitor = {false, ConfigSource::Default};
    ConfigOption<bool> listInputs = {false, ConfigSource::Default};
    ConfigOption<bool> listOutputs = {false, ConfigSource::Default};
    ConfigOption<bool> listChannels = {false, ConfigSource::Default};
};
