// ConfigParser.cpp

#include "ConfigParser.h"

#include <windows.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Defconf.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "cxxopts.hpp"

ConfigParser::ConfigParser(int argc, char** argv)
    : argc_(argc), argv_(argv) {}

std::string ConfigParser::Trim(const std::string& str) {
    const std::string whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

ToggleConfig ConfigParser::ParseToggleParameter(const std::string& toggleParam) {
    ToggleConfig toggleConfig;
    std::istringstream ss(toggleParam);
    std::string segment;
    std::vector<std::string> segments;

    while (std::getline(ss, segment, ':')) {
        segments.push_back(Trim(segment));
    }

    if (segments.size() != 3) {
        throw std::runtime_error("Invalid toggle parameter format. Expected format: type:index1:index2 (e.g., 'input:0:1')");
    }

    toggleConfig.type = segments[0];
    try {
        toggleConfig.index1 = std::stoi(segments[1]);
        toggleConfig.index2 = std::stoi(segments[2]);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Toggle indices must be integers.");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Toggle indices are out of range.");
    }

    return toggleConfig;
}

bool ConfigParser::SetupLogging(const Config& config) {
    LogLevel level = config.debug.value ? LogLevel::DEBUG : LogLevel::INFO;
    bool enableFileLogging = config.loggingEnabled.value;
    const std::string& filePath = config.logFilePath.value;

    try {
        if (!Logger::Instance().Initialize(level, enableFileLogging, filePath)) {
            return false;
        }

        if (enableFileLogging) {
            LOG_INFO("Logging initialized. Log file: " + filePath);
        } else {
            LOG_INFO("Logging initialized for console output.");
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return false;
    }
}

void ConfigParser::HandleConfiguration(Config& config) {
    // Parse command-line options first to get the config file path
    cxxopts::Options options = CreateOptions();
    cxxopts::ParseResult result;
    try {
        result = options.parse(argc_, argv_);
    } catch (const cxxopts::exceptions::parsing& e) {
        LOG_ERROR("Error parsing command-line options: " + std::string(e.what()));
        throw;
    }

    // Apply command-line options to get the config file path
    ApplyCommandLineOptions(result, config);

    // Now parse the config file using the possibly updated configFilePath
    ParseConfigFile(config.configFilePath.value, config);

    // Re-apply command-line options to override config file settings
    ApplyCommandLineOptions(result, config);

    // Now validate the final config
    ValidateConfig(config);

    if (!SetupLogging(config)) {
        throw std::runtime_error("Failed to setup logging.");
    }

    if (HandleSpecialCommands(config)) {
        exit(0);
    }

    LogConfiguration(config);

    LOG_DEBUG("Configuration handling completed.");
}

void ConfigParser::ValidateConfig(const Config& config) {
    if (config.index.value < 0) {
        throw std::runtime_error("Channel index must be non-negative");
    }

    if (config.voicemeeterType.value < VOICEMEETER_BASIC || config.voicemeeterType.value > VOICEMEETER_POTATO_X64) {
        throw std::runtime_error("Voicemeeter type must be one of the following: " +
                                 std::to_string(VOICEMEETER_BASIC) + " (Basic), " +
                                 std::to_string(VOICEMEETER_BANANA) + " (Banana), " +
                                 std::to_string(VOICEMEETER_POTATO) + " (Potato), " +
                                 std::to_string(VOICEMEETER_BASIC_X64) + " (Basic x64), " +
                                 std::to_string(VOICEMEETER_BANANA_X64) + " (Banana x64), or " +
                                 std::to_string(VOICEMEETER_POTATO_X64) + " (Potato x64)");
    }

    const std::string& type = Trim(config.type.value);
    if (type != "input" && type != "output") {
        throw std::runtime_error("Type must be either 'input' or 'output'");
    }

    if (config.pollingInterval.value < 10 || config.pollingInterval.value > 1000) {
        throw std::runtime_error("Polling interval must be between 10 and 1000 milliseconds");
    }

    // Validate hotkey key
    bool validKey = (
        (config.hotkeyVK.value >= 'A' && config.hotkeyVK.value <= 'Z') ||
        (config.hotkeyVK.value >= 'a' && config.hotkeyVK.value <= 'z') ||
        (config.hotkeyVK.value >= '0' && config.hotkeyVK.value <= '9') ||
        (config.hotkeyVK.value >= VK_F1 && config.hotkeyVK.value <= VK_F24)
    );

    if (!validKey) {
        throw std::runtime_error("Hotkey key must be an alphanumeric character or a function key (F1-F24).");
    }

    // Validate hotkey modifiers
    int modifiers = config.hotkeyModifiers.value;
    if (!(modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN))) {
        throw std::runtime_error("Hotkey modifiers must include at least one of MOD_CONTROL, MOD_ALT, MOD_SHIFT, or MOD_WIN.");
    }

    // Additional validations...
}

void ConfigParser::ParseConfigFile(const std::string& configPath, Config& config) {
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        LOG_INFO("Config file not found: " + configPath + ". Continuing with command line flags.");
        return;
    }

    LOG_DEBUG("Parsing config file: " + configPath);
    std::string line;
    while (std::getline(configFile, line)) {
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        line = Trim(line);
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            key = Trim(key);
            value = Trim(value);

            LOG_DEBUG("Parsing config key: " + key + " = " + value);

            try {
                if (key == "chime") {
                    config.chime.value = (value == "true");
                    config.chime.source = ConfigSource::ConfigFile;
                } else if (key == "debug") {
                    config.debug.value = (value == "true");
                    config.debug.source = ConfigSource::ConfigFile;
                } else if (key == "voicemeeter") {
                    config.voicemeeterType.value = std::stoi(value);
                    config.voicemeeterType.source = ConfigSource::ConfigFile;
                } else if (key == "monitor") {
                    config.monitorDeviceUUID.value = value;
                    config.monitorDeviceUUID.source = ConfigSource::ConfigFile;
                } else if (key == "toggle") {
                    config.toggleParam.value = value;
                    config.toggleParam.source = ConfigSource::ConfigFile;
                } else if (key == "polling") {
                    config.pollingEnabled.value = true;
                    config.pollingInterval.value = std::stoi(value);
                    config.pollingEnabled.source = ConfigSource::ConfigFile;
                    config.pollingInterval.source = ConfigSource::ConfigFile;
                } else if (key == "startup_sound") {
                    config.startupSound.value = (value == "true");
                    config.startupSound.source = ConfigSource::ConfigFile;
                } else if (key == "startup_volume") {
                    config.startupVolumePercent.value = std::stoi(value);
                    config.startupVolumePercent.source = ConfigSource::ConfigFile;
                }
                else if (key == "hotkey_modifiers") {
                    config.hotkeyModifiers.value = std::stoi(value);
                    config.hotkeyModifiers.source = ConfigSource::ConfigFile;
                } 
                else if (key == "hotkey_key") {
                    config.hotkeyVK.value = std::stoi(value);
                    config.hotkeyVK.source = ConfigSource::ConfigFile;
                }

                // Add additional config keys as necessary
            } catch (const std::exception& e) {
                LOG_ERROR("Error parsing config key " + key + ": " + e.what());
            }
        }
    }
    LOG_DEBUG("Finished parsing config file");
}

void ConfigParser::ValidateOptions(const cxxopts::ParseResult& result) {
    if (result.count("index")) {
        int index = result["index"].as<int>();
        if (index < 0) {
            throw std::runtime_error("Channel index must be non-negative");
        }
    }
    if (result.count("voicemeeter")) {
        int type = result["voicemeeter"].as<int>();
        if (type < VOICEMEETER_BASIC || type > VOICEMEETER_POTATO_X64) {
            throw std::runtime_error("Voicemeeter type must be " +
                                     std::to_string(VOICEMEETER_BASIC) + " (Basic), " +
                                     std::to_string(VOICEMEETER_BANANA) + " (Banana), or " +
                                     std::to_string(VOICEMEETER_POTATO) + " (Potato)");
        }
    }

    if (result.count("type")) {
        std::string type = Trim(result["type"].as<std::string>());
        if (type != "input" && type != "output") {
            throw std::runtime_error("Type must be either 'input' or 'output'");
        }
    }

    if (result.count("polling-interval")) {
        int interval = result["polling-interval"].as<int>();
        if (interval < 10 || interval > 1000) {
            throw std::runtime_error("Polling interval must be between 10 and 1000 milliseconds");
        }
    }

    // Additional validations...
}

cxxopts::Options ConfigParser::CreateOptions() {
    cxxopts::Options options("VoiceMirror", "Synchronize Windows Volume with Voicemeeter virtual channels");

    options.add_options()
        ("C,chime", "Enable chime sound on sync from Voicemeeter to Windows")
        ("L,list-channels", "List all Voicemeeter channels with their labels and exit")
        ("S,shutdown", "Shutdown all instances of the app and exit immediately")
        ("H,hidden", "Hide the console window. Use with --log to run without showing the console.")
        ("I,list-inputs", "List available Voicemeeter virtual inputs and exit")
        ("M,list-monitor", "List monitorable audio devices and exit")
        ("O,list-outputs", "List available Voicemeeter virtual outputs and exit")
        ("V,voicemeeter", "Specify which Voicemeeter to use (1: Basic, 2: Banana, 3: Potato)", 
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_VOICEMEETER_TYPE)))
        ("i,index", "Specify the Voicemeeter virtual channel index to use", 
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_CHANNEL_INDEX)))
        ("min", "Minimum dBm for Voicemeeter channel", 
            cxxopts::value<float>()->default_value(std::to_string(DEFAULT_MIN_DBM)))
        ("max", "Maximum dBm for Voicemeeter channel", 
            cxxopts::value<float>()->default_value(std::to_string(DEFAULT_MAX_DBM)))
        ("p,polling-interval", "Enable polling mode with interval in milliseconds", 
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_POLLING_INTERVAL_MS)))
        ("s,startup-volume", "Set the initial Windows volume level as a percentage (0-100)", 
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_STARTUP_VOLUME_PERCENT)))
        ("T,toggle", "Toggle parameter", 
            cxxopts::value<std::string>()->default_value(DEFAULT_TOGGLE_COMMAND))
        ("d,debug", "Enable debug logging mode")
        ("c,config", "Path to configuration file", 
            cxxopts::value<std::string>()->default_value(DEFAULT_CONFIG_FILE))
        // Hotkey Configuration
        ("hm,hotkey-modifiers", "Hotkey modifiers (e.g., Ctrl=2, Alt=1, Shift=4, Win=8). Combine using bitwise OR (e.g., 3 for Ctrl+Alt)",
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_HOTKEY_MODIFIERS)))
        ("hk,hotkey-key", "Hotkey virtual key code (e.g., R=82, F5=116)",
            cxxopts::value<int>()->default_value(std::to_string(DEFAULT_HOTKEY_VK)))
        ;

    options.add_options("Advanced")
        ("m,monitor", "Specify the monitor device UUID", 
            cxxopts::value<std::string>()->default_value(DEFAULT_MONITOR_DEVICE_UUID))
        ("log", "Enable logging with specified log file path", 
            cxxopts::value<std::string>()->default_value(DEFAULT_LOG_FILE))
        ("startup-sound", "Enable startup sound", 
            cxxopts::value<bool>()->default_value("false"));

    options.add_options("Help")
        ("help", "Print help")
        ("version", "Print version");

    return options;
}

void ConfigParser::ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config) {
    if (result.count("config")) {
        config.configFilePath.value = result["config"].as<std::string>();
        config.configFilePath.source = ConfigSource::CommandLine;
    }

    if (result.count("list-monitor")) {
        config.listMonitor.value = true;
        config.listMonitor.source = ConfigSource::CommandLine;
    }
    if (result.count("list-inputs")) {
        config.listInputs.value = true;
        config.listInputs.source = ConfigSource::CommandLine;
    }
    if (result.count("list-outputs")) {
        config.listOutputs.value = true;
        config.listOutputs.source = ConfigSource::CommandLine;
    }
    if (result.count("list-channels")) {
        config.listChannels.value = true;
        config.listChannels.source = ConfigSource::CommandLine;
    }
    if (result.count("index")) {
        config.index.value = result["index"].as<int>();
        config.index.source = ConfigSource::CommandLine;
    }
    if (result.count("type")) {
        config.type.value = Trim(result["type"].as<std::string>());
        config.type.source = ConfigSource::CommandLine;
    }
    if (result.count("min")) {
        config.minDbm.value = result["min"].as<float>();
        config.minDbm.source = ConfigSource::CommandLine;
    }
    if (result.count("max")) {
        config.maxDbm.value = result["max"].as<float>();
        config.maxDbm.source = ConfigSource::CommandLine;
    }
    if (result.count("voicemeeter")) {
        config.voicemeeterType.value = result["voicemeeter"].as<int>();
        config.voicemeeterType.source = ConfigSource::CommandLine;
    }
    if (result.count("debug")) {
        config.debug.value = true;
        config.debug.source = ConfigSource::CommandLine;
    }
    if (result.count("chime")) {
        config.chime.value = true;
        config.chime.source = ConfigSource::CommandLine;
    }
    if (result.count("monitor")) {
        config.monitorDeviceUUID.value = result["monitor"].as<std::string>();
        config.monitorDeviceUUID.source = ConfigSource::CommandLine;
    }
    if (result.count("log")) {
        config.loggingEnabled.value = true;
        config.logFilePath.value = result["log"].as<std::string>();
        config.loggingEnabled.source = ConfigSource::CommandLine;
        config.logFilePath.source = ConfigSource::CommandLine;
    }
    if (result.count("hidden")) {
        config.hideConsole.value = true;
        config.hideConsole.source = ConfigSource::CommandLine;
    }
    if (result.count("toggle")) {
        config.toggleParam.value = result["toggle"].as<std::string>();
        config.toggleParam.source = ConfigSource::CommandLine;
    }
    if (result.count("shutdown")) {
        config.shutdown.value = true;
        config.shutdown.source = ConfigSource::CommandLine;
    }
    if (result.count("help")) {
        config.help.value = true;
        config.help.source = ConfigSource::CommandLine;
    }
    if (result.count("version")) {
        config.version.value = true;
        config.version.source = ConfigSource::CommandLine;
    }
    if (result.count("polling-interval")) {
        config.pollingEnabled.value = true;
        config.pollingInterval.value = result["polling-interval"].as<int>();
        config.pollingEnabled.source = ConfigSource::CommandLine;
        config.pollingInterval.source = ConfigSource::CommandLine;
    }
    if (result.count("startup-volume")) {
        config.startupVolumePercent.value = result["startup-volume"].as<int>();
        config.startupVolumePercent.source = ConfigSource::CommandLine;
    }
    if (result.count("startup-sound")) {
        config.startupSound.value = result["startup-sound"].as<bool>();
        config.startupSound.source = ConfigSource::CommandLine;
    }
    if (result.count("toggle-command")) {
        config.toggleCommand.value = result["toggle-command"].as<std::string>();
        config.toggleCommand.source = ConfigSource::CommandLine;
    }
    if (result.count("hotkey-modifiers")) {
        config.hotkeyModifiers.value = result["hotkey-modifiers"].as<int>();
        config.hotkeyModifiers.source = ConfigSource::CommandLine;
    }
    if (result.count("hotkey-key")) {
        config.hotkeyVK.value = result["hotkey-key"].as<int>();
        config.hotkeyVK.source = ConfigSource::CommandLine;
    }
}

void ConfigParser::LogConfiguration(const Config& config) {
    std::ostringstream oss;
    oss << "Startup Configuration:\n";

    auto logOption = [&](const std::string& name, const std::string& value, ConfigSource source) {
        std::string sourceStr;
        switch (source) {
            case ConfigSource::Default:
                sourceStr = "[def]";
                break;
            case ConfigSource::ConfigFile:
                sourceStr = "[conf]";
                break;
            case ConfigSource::CommandLine:
                sourceStr = "[cmd]";
                break;
        }
        oss << sourceStr << " " << name << ": " << value << "\n";
    };

    // File Paths
    logOption("configFilePath", config.configFilePath.value, config.configFilePath.source);
    logOption("logFilePath", config.logFilePath.value, config.logFilePath.source);

    // Debugging and Logging
    logOption("debug", config.debug.value ? "true" : "false", config.debug.source);
    logOption("loggingEnabled", config.loggingEnabled.value ? "true" : "false", config.loggingEnabled.source);

    // Application Behavior
    logOption("help", config.help.value ? "true" : "false", config.help.source);
    logOption("version", config.version.value ? "true" : "false", config.version.source);
    logOption("hideConsole", config.hideConsole.value ? "true" : "false", config.hideConsole.source);
    logOption("shutdown", config.shutdown.value ? "true" : "false", config.shutdown.source);
    logOption("chime", config.chime.value ? "true" : "false", config.chime.source);
    logOption("pollingEnabled", config.pollingEnabled.value ? "true" : "false", config.pollingEnabled.source);
    logOption("startupSound", config.startupSound.value ? "true" : "false", config.startupSound.source);

    // Volume Settings
    logOption("startupVolumePercent", std::to_string(config.startupVolumePercent.value), config.startupVolumePercent.source);

    // Voicemeeter Settings
    logOption("voicemeeterType", std::to_string(config.voicemeeterType.value), config.voicemeeterType.source);
    logOption("index", std::to_string(config.index.value), config.index.source);

    // Audio Levels
    logOption("maxDbm", std::to_string(config.maxDbm.value), config.maxDbm.source);
    logOption("minDbm", std::to_string(config.minDbm.value), config.minDbm.source);

    // Device and Toggle Settings
    logOption("monitorDeviceUUID", config.monitorDeviceUUID.value, config.monitorDeviceUUID.source);
    logOption("toggleParam", config.toggleParam.value, config.toggleParam.source);
    logOption("toggleCommand", config.toggleCommand.value, config.toggleCommand.source);

    // Polling Settings
    logOption("pollingInterval", std::to_string(config.pollingInterval.value), config.pollingInterval.source);

    // Channel Type
    logOption("type", config.type.value, config.type.source);

    // Listing Flags
    logOption("listMonitor", config.listMonitor.value ? "true" : "false", config.listMonitor.source);
    logOption("listInputs", config.listInputs.value ? "true" : "false", config.listInputs.source);
    logOption("listOutputs", config.listOutputs.value ? "true" : "false", config.listOutputs.source);
    logOption("listChannels", config.listChannels.value ? "true" : "false", config.listChannels.source);

    // Hotkey Settings
    logOption("hotkeyModifiers", std::to_string(config.hotkeyModifiers.value), config.hotkeyModifiers.source);
    logOption("hotkeyVK", std::to_string(config.hotkeyVK.value), config.hotkeyVK.source);

    LOG_DEBUG(oss.str());
}

bool ConfigParser::HandleSpecialCommands(const Config& config) {
    LOG_DEBUG("Handling special commands");

    // Use .value to access the actual boolean value
    if (config.help.value) {
        cxxopts::Options options = CreateOptions();
        std::cout << options.help() << std::endl;
        return true;
    }

    if (config.version.value) {
        std::string versionStr = "VoiceMirror Version " +
                                 std::to_string(VERSION_MAJOR) + "." +
                                 std::to_string(VERSION_MINOR) + "." +
                                 std::to_string(VERSION_PATCH);
        if (!std::string(VERSION_PRE_RELEASE).empty()) {
            versionStr += "-" + std::string(VERSION_PRE_RELEASE);
        }
        LOG_INFO(versionStr);
        return true;
    }

    if (config.shutdown.value) {
        LOG_DEBUG("Processing shutdown command");
        RAIIHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent.get()) {
            bool signalResult = SetEvent(hQuitEvent.get());
            if (!signalResult) {
                LOG_ERROR("Failed to signal quit event to running instances.");
                return true;
            }
            LOG_INFO("Signaled running instances to quit.");
        } else {
            LOG_INFO("No running instances found.");
        }
        return true;
    }

    return false;
}
