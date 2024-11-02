#pragma once

#include <string>

// Mutex and event names for application control
constexpr char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";      // Mutex to prevent multiple instances
constexpr char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";  // Event to signal application shutdown

// Default paths to Voicemeeter DLLs based on system architecture
constexpr const char* DEFAULT_DLL_PATH_64 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote64.dll";  // 64-bit DLL path
constexpr const char* DEFAULT_DLL_PATH_32 = "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote.dll";   // 32-bit DLL path

// Voicemeeter settings and configuration defaults
constexpr int DEFAULT_CHANNEL_INDEX = 3;                     // Default channel index
constexpr int DEFAULT_VOICEMEETER_TYPE = 2;                  // Default Voicemeeter type (e.g., Banana)
constexpr int DEFAULT_POLLING_INTERVAL_MS = 100;             // Default polling interval in milliseconds

// Retry settings for API initialization
constexpr int MAX_RETRIES = 20;                              // Max retries to check if Voicemeeter is running
constexpr int RETRY_DELAY_MS = 1000;                         // Delay between retries in milliseconds

// Configuration file and logging
constexpr const char* DEFAULT_CONFIG_FILE = "VoiceMirror.conf"; // Default configuration file path
constexpr const char* DEFAULT_LOG_FILE = "VoiceMirror.log";     // Default log file path

// Audio levels
constexpr float DEFAULT_MIN_DBM = -60.0f;                    // Minimum dB level for audio
constexpr float DEFAULT_MAX_DBM = 12.0f;                     // Maximum dB level for audio
constexpr int DEFAULT_STARTUP_VOLUME_PERCENT = -1;           // Default startup volume percentage

// Enum for Voicemeeter Types
enum VoicemeeterType {
    VOICEMEETER_BASIC = 1,
    VOICEMEETER_BANANA,
    VOICEMEETER_POTATO
};

// Config structure to hold configurable settings with defaults
struct Config {
    std::string configFilePath = DEFAULT_CONFIG_FILE;   // Path to config file
    bool debug = false;                                 // Debug mode
    bool help = false;                                  // Help flag
    bool hideConsole = false;                           // Console visibility
    int index = DEFAULT_CHANNEL_INDEX;                  // Channel index
    bool listChannels = false;                          // Flag to list channels
    bool listInputs = false;                            // Flag to list inputs
    bool listMonitor = false;                           // Flag to list monitor devices
    bool listOutputs = false;                           // Flag to list outputs
    std::string logFilePath = DEFAULT_LOG_FILE;         // Path to log file
    bool loggingEnabled = false;                        // Enable logging
    float maxDbm = DEFAULT_MAX_DBM;                     // Max dBm level
    float minDbm = DEFAULT_MIN_DBM;                     // Min dBm level
    std::string monitorDeviceUUID;                      // Monitor device UUID
    bool pollingEnabled = false;                        // Enable polling
    int pollingInterval = DEFAULT_POLLING_INTERVAL_MS;  // Polling interval
    bool shutdown = false;                              // Shutdown flag
    bool chime = false;                                 // Chime notification
    int startupVolumePercent = DEFAULT_STARTUP_VOLUME_PERCENT; // Startup volume percentage
    bool startupSound = false;                          // Startup sound flag
    std::string toggleParam;                            // Toggle parameter
    std::string type = "input";                         // Default type (input/output)
    bool version = false;                               // Version flag
    int voicemeeterType = DEFAULT_VOICEMEETER_TYPE;     // Voicemeeter type
};
