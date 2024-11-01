#pragma once
#include <string>


// Application constants

constexpr char MUTEX_NAME[] = "Global\\VoiceMirrorMutex";      // Mutex name
constexpr char EVENT_NAME[] = "Global\\VoiceMirrorQuitEvent";  // Quit event name
constexpr int DEFAULT_CHANNEL_INDEX = 3;
constexpr float DEFAULT_MIN_DBM = -60.0f;
constexpr float DEFAULT_MAX_DBM = 12.0f;
constexpr int DEFAULT_VOICEMEETER_TYPE = 2;
constexpr int DEFAULT_POLLING_INTERVAL = 100;
constexpr const char *DEFAULT_CONFIG_FILE = "VoiceMirror.conf";

// Configuration structure
struct Config {
    bool listMonitor = false;
    bool listInputs = false;
    bool listOutputs = false;
    bool listChannels = false;
    int index = 3;
    std::string type = "input";
    float minDbm = -60.0f;
    float maxDbm = 12.0f;
    int voicemeeterType = 2;
    bool debug = false;
    bool version = false;
    bool help = false;
    bool sound = false;
    std::string monitorDeviceUUID;
    std::string logFilePath;
    bool loggingEnabled = false;
    bool hideConsole = false;
    std::string configFilePath;
    std::string toggleParam;
    bool shutdown = false;
    bool pollingEnabled = false;
    int pollingInterval = 100;
};
