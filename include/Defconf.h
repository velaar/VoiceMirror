// Defconf.h
#pragma once

#include <windows.h>
#include <cstdint>

// -----------------------------
// Mutex and Event Names
// -----------------------------

constexpr const char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";
constexpr const char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";
constexpr const char COM_INIT_MUTEX_NAME[] = "Global\\VoiceMirrorCOMInitMutex";

// -----------------------------
// Default Paths
// -----------------------------

constexpr const char* DEFAULT_DLL_PATH_64 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote64.dll";
constexpr const char* DEFAULT_DLL_PATH_32 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote.dll";

constexpr const char* DEFAULT_CONFIG_FILE = "VoiceMirror.conf";
constexpr const char* DEFAULT_LOG_FILE = "VoiceMirror.log";
constexpr const char* DEFAULT_STARTUP_SOUND_FILE = "m95.mp3";
constexpr const wchar_t* DEFAULT_SYNC_SOUND_FILE = L"C:\\Windows\\Media\\Windows Unlock.wav";

// -----------------------------
// Voicemeeter Settings and Configuration Defaults
// -----------------------------
constexpr WORD DEBUG_COLOR = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD INFO_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;

constexpr uint8_t DEFAULT_CHANNEL_INDEX = 3;
constexpr uint8_t DEFAULT_VOICEMEETER_TYPE = 2;
constexpr uint16_t DEFAULT_POLLING_INTERVAL_MS = 100;
constexpr uint16_t DEFAULT_STARTUP_SOUND_DELAY_MS = 1250;
constexpr uint16_t DEBOUNCE_DURATION_MS = 250;
constexpr uint16_t SUPPRESSION_DURATION_MS = DEBOUNCE_DURATION_MS;
constexpr uint8_t MAX_RETRIES = 20;
constexpr uint16_t RETRY_DELAY_MS = 1000;

// -----------------------------
// Chime Settings
// -----------------------------

constexpr const wchar_t* SYNC_SOUND_FILE_PATH = L"C:\\Windows\\Media\\Windows Unlock.wav";
constexpr const wchar_t* SYNC_FALLBACK_SOUND_ALIAS = L"SystemAsterisk";

// -----------------------------
// Audio Level Boundaries and Defaults
// -----------------------------

constexpr int8_t DEFAULT_MIN_DBM = -60;
constexpr int8_t DEFAULT_MAX_DBM = 12;

constexpr int8_t DEFAULT_STARTUP_VOLUME_PERCENT = -1;

// -----------------------------
// Toggle Parameters
// -----------------------------

constexpr const char* DEFAULT_TOGGLE_PARAM = "input:0:1";
constexpr const char* DEFAULT_TOGGLE_COMMAND = "";

// -----------------------------
// Application Behavior Defaults
// -----------------------------

constexpr bool DEFAULT_DEBUG_MODE = 
#ifdef _DEBUG
    true
#else
    false
#endif
;

constexpr bool DEFAULT_HIDDEN_CONSOLE = false;
constexpr bool DEFAULT_LOGGING_ENABLED = false;
constexpr bool DEFAULT_CHIME_ENABLED = false;
constexpr bool DEFAULT_POLLING_ENABLED = false;
constexpr bool DEFAULT_SHUTDOWN_ENABLED = false;
constexpr bool DEFAULT_STARTUP_SOUND_ENABLED = false;
constexpr bool DEFAULT_HELP_FLAG = false;
constexpr bool DEFAULT_VERSION_FLAG = false;

constexpr uint8_t VOICEMEETER_MANAGER_RETRIES = 10;

// -----------------------------
// Command-Line Option Defaults
// -----------------------------

constexpr const char* DEFAULT_TYPE = "input";
constexpr const char* DEFAULT_MONITOR_DEVICE_UUID = "";

// -----------------------------
// Version Information
// -----------------------------

constexpr uint8_t VERSION_MAJOR = 0;
constexpr uint8_t VERSION_MINOR = 2;
constexpr uint8_t VERSION_PATCH = 0;
constexpr const char* VERSION_PRE_RELEASE = "alpha";

// -----------------------------
// Voicemeeter Type Enumeration
// -----------------------------

enum VoicemeeterType : uint8_t {
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

enum class ChannelType : uint8_t {
    Input,
    Output
};

// -----------------------------
// Toggle Configuration Structure
// -----------------------------

struct ToggleConfig {
    const char* type;  // Channel type
    uint8_t index1;    // First channel index
    uint8_t index2;    // Second channel index
};

enum class ConfigSource : uint8_t {
    Default,
    ConfigFile,
    CommandLine
};

enum class LogLevel {
    DEBUG,    ///< Debug level for detailed internal information.
    INFO,     ///< Informational messages that highlight the progress.
    WARNING,  ///< Potentially harmful situations.
    ERR       ///< Error events that might still allow the application to continue.
};

template <typename T>
struct ConfigOption {
    T value;
    ConfigSource source = ConfigSource::Default;
};

enum class ChangeSource : uint8_t {
    None,
    Windows,
    Voicemeeter
};

// -----------------------------
// Hotkey Settings
// -----------------------------

constexpr uint16_t DEFAULT_HOTKEY_MODIFIERS = MOD_CONTROL | MOD_ALT;
constexpr uint8_t DEFAULT_HOTKEY_VK = 'R';

// -----------------------------
// Configuration Structure
// -----------------------------

struct Config {
    // File Paths
    ConfigOption<const char*> configFilePath = {DEFAULT_CONFIG_FILE, ConfigSource::Default};
    ConfigOption<const char*> logFilePath = {DEFAULT_LOG_FILE, ConfigSource::Default};

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
    ConfigOption<int8_t> startupVolumePercent = {DEFAULT_STARTUP_VOLUME_PERCENT, ConfigSource::Default};

    // Voicemeeter Settings
    ConfigOption<uint8_t> voicemeeterType = {DEFAULT_VOICEMEETER_TYPE, ConfigSource::Default};
    ConfigOption<uint8_t> index = {DEFAULT_CHANNEL_INDEX, ConfigSource::Default};

    // Audio Levels
    ConfigOption<int8_t> maxDbm = {DEFAULT_MAX_DBM, ConfigSource::Default};
    ConfigOption<int8_t> minDbm = {DEFAULT_MIN_DBM, ConfigSource::Default};

    // Device and Toggle Settings
    ConfigOption<const char*> monitorDeviceUUID = {DEFAULT_MONITOR_DEVICE_UUID, ConfigSource::Default};
    ConfigOption<const char*> toggleParam = {DEFAULT_TOGGLE_PARAM, ConfigSource::Default};
    ConfigOption<const char*> toggleCommand = {DEFAULT_TOGGLE_COMMAND, ConfigSource::Default};

    // Polling Settings
    ConfigOption<uint16_t> pollingInterval = {DEFAULT_POLLING_INTERVAL_MS, ConfigSource::Default};

    // Channel Type
    ConfigOption<const char*> type = {DEFAULT_TYPE, ConfigSource::Default};

    // Listing Flags
    ConfigOption<bool> listMonitor = {false, ConfigSource::Default};
    ConfigOption<bool> listInputs = {false, ConfigSource::Default};
    ConfigOption<bool> listOutputs = {false, ConfigSource::Default};
    ConfigOption<bool> listChannels = {false, ConfigSource::Default};

    // Hotkey Settings
    ConfigOption<uint16_t> hotkeyModifiers = {DEFAULT_HOTKEY_MODIFIERS, ConfigSource::Default};
    ConfigOption<uint8_t> hotkeyVK = {DEFAULT_HOTKEY_VK, ConfigSource::Default};

    // Sound Settings
    ConfigOption<const wchar_t*> syncSoundFilePath = {DEFAULT_SYNC_SOUND_FILE, ConfigSource::Default};
    ConfigOption<const char*> startupSoundFilePath = {DEFAULT_STARTUP_SOUND_FILE, ConfigSource::Default};
    ConfigOption<uint16_t> startupSoundDelay = {DEFAULT_STARTUP_SOUND_DELAY_MS, ConfigSource::Default};
};
