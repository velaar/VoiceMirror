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
        segments.emplace_back(Trim(segment));
    }

    if (segments.size() != 3) {
        LOG_ERROR("[ConfigParser::ParseToggleParameter] Invalid toggle parameter format: " + toggleParam);
        throw std::runtime_error("Invalid toggle parameter format. Expected format: type:index1:index2 (e.g., 'input:0:1')");
    }

    toggleConfig.type = segments[0].c_str();
    try {
        toggleConfig.index1 = static_cast<uint8_t>(std::stoi(segments[1]));
        toggleConfig.index2 = static_cast<uint8_t>(std::stoi(segments[2]));
    } catch (...) {
        LOG_ERROR("[ConfigParser::ParseToggleParameter] Toggle indices must be valid integers.");
        throw std::runtime_error("Toggle indices must be valid integers.");
    }

    LOG_DEBUG("[ConfigParser::ParseToggleParameter] Parsed toggle parameter successfully: " + toggleParam);
    return toggleConfig;
}

bool ConfigParser::SetupLogging(const Config& config) {
    LogLevel level = config.debug.value ? LogLevel::DEBUG : LogLevel::INFO;
    bool enableFileLogging = config.loggingEnabled.value;
    std::string filePath = config.logFilePath.value ? config.logFilePath.value : "";

    try {
        if (!Logger::Instance().Initialize(level, enableFileLogging, filePath)) {
            LOG_ERROR("[ConfigParser::SetupLogging] Failed to initialize logger.");
            return false;
        }

        LOG_INFO("[ConfigParser::SetupLogging] Logging initialized. " + 
                 (enableFileLogging ? "Log file: " + filePath : "Console output only."));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[ConfigParser::SetupLogging] Exception during logger setup: " + std::string(e.what()));
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return false;
    }
}

void ConfigParser::HandleConfiguration(Config& config) {
    cxxopts::Options options = CreateOptions();
    cxxopts::ParseResult result;
    try {
        result = options.parse(argc_, argv_);
    } catch (const cxxopts::exceptions::parsing& e) {
        LOG_ERROR("[ConfigParser::HandleConfiguration] Error parsing command-line options: " + std::string(e.what()));
        throw;
    }

    ApplyCommandLineOptions(result, config);
    ParseConfigFile(config.configFilePath.value, config);
    ApplyCommandLineOptions(result, config);
    ValidateConfig(config);

    if (!SetupLogging(config)) {
        throw std::runtime_error("Failed to setup logging.");
    }

    if (HandleSpecialCommands(config)) {
        exit(0);
    }

    LogConfiguration(config);
    LOG_DEBUG("[ConfigParser::HandleConfiguration] Configuration handling completed.");
}

void ConfigParser::ValidateConfig(const Config& config) {
    if (config.index.value < 0) {
        LOG_ERROR("[ConfigParser::ValidateConfig] Channel index must be non-negative.");
        throw std::runtime_error("Channel index must be non-negative");
    }

    if (config.voicemeeterType.value < VOICEMEETER_BASIC || config.voicemeeterType.value > VOICEMEETER_POTATO_X64) {
        LOG_ERROR("[ConfigParser::ValidateConfig] Voicemeeter type out of range: " + std::to_string(config.voicemeeterType.value));
        throw std::runtime_error("Voicemeeter type must be between 1 and 6.");
    }

    std::string type = Trim(config.type.value);
    if (type != "input" && type != "output") {
        LOG_ERROR("[ConfigParser::ValidateConfig] Invalid type: " + type);
        throw std::runtime_error("Type must be either 'input' or 'output'");
    }

    if (config.pollingInterval.value < 10 || config.pollingInterval.value > 1000) {
        LOG_ERROR("[ConfigParser::ValidateConfig] Polling interval out of range: " + std::to_string(config.pollingInterval.value));
        throw std::runtime_error("Polling interval must be between 10 and 1000 milliseconds");
    }

    bool validKey = (
        (config.hotkeyVK.value >= 'A' && config.hotkeyVK.value <= 'Z') ||
        (config.hotkeyVK.value >= 'a' && config.hotkeyVK.value <= 'z') ||
        (config.hotkeyVK.value >= '0' && config.hotkeyVK.value <= '9') ||
        (config.hotkeyVK.value >= VK_F1 && config.hotkeyVK.value <= VK_F24)
    );

    if (!validKey) {
        LOG_ERROR("[ConfigParser::ValidateConfig] Hotkey must be alphanumeric or F1-F24.");
        throw std::runtime_error("Hotkey key must be an alphanumeric character or a function key (F1-F24).");
    }

    uint16_t modifiers = config.hotkeyModifiers.value;
    if (!(modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN))) {
        LOG_ERROR("[ConfigParser::ValidateConfig] Invalid hotkey modifiers.");
        throw std::runtime_error("Hotkey modifiers must include at least one of MOD_CONTROL, MOD_ALT, MOD_SHIFT, or MOD_WIN.");
    }
}

void ConfigParser::ParseConfigFile(const std::string& configPath, Config& config) {
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        LOG_INFO("[ConfigParser::ParseConfigFile] Config file not found: " + configPath + ". Continuing with command line flags.");
        return;
    }

    LOG_DEBUG("[ConfigParser::ParseConfigFile] Parsing config file: " + configPath);
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
            LOG_DEBUG("[ConfigParser::ParseConfigFile] Parsing config key: " + key + " = " + value);

            try {
                if (key == "chime") {
                    config.chime.value = (value == "true");
                    config.chime.source = ConfigSource::ConfigFile;
                } else if (key == "debug") {
                    config.debug.value = (value == "true");
                    config.debug.source = ConfigSource::ConfigFile;
                } else if (key == "voicemeeter") {
                    config.voicemeeterType.value = static_cast<uint8_t>(std::stoi(value));
                    config.voicemeeterType.source = ConfigSource::ConfigFile;
                } else if (key == "monitor") {
                    config.monitorDeviceUUID.value = value.c_str();
                    config.monitorDeviceUUID.source = ConfigSource::ConfigFile;
                } else if (key == "toggle") {
                    config.toggleParam.value = value.c_str();
                    config.toggleParam.source = ConfigSource::ConfigFile;
                } else if (key == "polling") {
                    config.pollingEnabled.value = true;
                    config.pollingInterval.value = static_cast<uint16_t>(std::stoi(value));
                    config.pollingEnabled.source = ConfigSource::ConfigFile;
                    config.pollingInterval.source = ConfigSource::ConfigFile;
                } else if (key == "startup_sound") {
                    config.startupSound.value = (value == "true");
                    config.startupSound.source = ConfigSource::ConfigFile;
                } else if (key == "startup_volume") {
                    config.startupVolumePercent.value = static_cast<int8_t>(std::stoi(value));
                    config.startupVolumePercent.source = ConfigSource::ConfigFile;
                } else if (key == "hotkey_modifiers") {
                    config.hotkeyModifiers.value = static_cast<uint16_t>(std::stoi(value));
                    config.hotkeyModifiers.source = ConfigSource::ConfigFile;
                } else if (key == "hotkey_key") {
                    config.hotkeyVK.value = static_cast<uint8_t>(std::stoi(value));
                    config.hotkeyVK.source = ConfigSource::ConfigFile;
                } else if (key == "log") {
                    config.loggingEnabled.value = true;
                    config.logFilePath.value = value.c_str();
                    config.loggingEnabled.source = ConfigSource::ConfigFile;
                    config.logFilePath.source = ConfigSource::ConfigFile;
                }
            } catch (...) {
                LOG_ERROR("[ConfigParser::ParseConfigFile] Error parsing config key " + key);
            }
        }
    }
    LOG_DEBUG("[ConfigParser::ParseConfigFile] Finished parsing config file");
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
            cxxopts::value<uint8_t>()->default_value(std::to_string(DEFAULT_VOICEMEETER_TYPE)))
        ("i,index", "Specify the Voicemeeter virtual channel index to use", 
            cxxopts::value<uint8_t>()->default_value(std::to_string(DEFAULT_CHANNEL_INDEX)))
        ("min", "Minimum dBm for Voicemeeter channel", 
            cxxopts::value<int8_t>()->default_value(std::to_string(DEFAULT_MIN_DBM)))
        ("max", "Maximum dBm for Voicemeeter channel", 
            cxxopts::value<int8_t>()->default_value(std::to_string(DEFAULT_MAX_DBM)))
        ("p,polling-interval", "Enable polling mode with interval in milliseconds", 
            cxxopts::value<uint16_t>()->default_value(std::to_string(DEFAULT_POLLING_INTERVAL_MS)))
        ("s,startup-volume", "Set the initial Windows volume level as a percentage (0-100)", 
            cxxopts::value<int8_t>()->default_value(std::to_string(DEFAULT_STARTUP_VOLUME_PERCENT)))
        ("T,toggle", "Toggle parameter", 
            cxxopts::value<std::string>()->default_value(DEFAULT_TOGGLE_PARAM))
        ("d,debug", "Enable debug logging mode")
        ("c,config", "Path to configuration file", 
            cxxopts::value<std::string>()->default_value(DEFAULT_CONFIG_FILE))
        ("hm,hotkey-modifiers", "Hotkey modifiers (e.g., Ctrl=2, Alt=1, Shift=4, Win=8). Combine using bitwise OR (e.g., 3 for Ctrl+Alt)",
            cxxopts::value<uint16_t>()->default_value(std::to_string(DEFAULT_HOTKEY_MODIFIERS)))
        ("hk,hotkey-key", "Hotkey virtual key code (e.g., R=82, F5=116)",
            cxxopts::value<uint8_t>()->default_value(std::to_string(DEFAULT_HOTKEY_VK)))
        ("log", "Enable logging with specified log file path", 
            cxxopts::value<std::string>()->default_value(DEFAULT_LOG_FILE))
        ("startup-sound", "Enable startup sound", 
            cxxopts::value<bool>()->default_value("false"))
        ("help", "Print help")
        ("version", "Print version");

    return options;
}

void ConfigParser::ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config) {
    if (result.count("config")) {
        config.configFilePath.value = result["config"].as<std::string>().c_str();
        config.configFilePath.source = ConfigSource::CommandLine;
        LOG_DEBUG(std::string("[ConfigParser::ApplyCommandLineOptions] Config file path set to: ") + config.configFilePath.value);
    }

    auto setBool = [&](const std::string& key, ConfigOption<bool>& option) {
        if (result.count(key)) {
            option.value = true;
            option.source = ConfigSource::CommandLine;
            LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] " + key + " set to true from command line.");
        }
    };

    setBool("list-monitor", config.listMonitor);
    setBool("list-inputs", config.listInputs);
    setBool("list-outputs", config.listOutputs);
    setBool("list-channels", config.listChannels);
    setBool("debug", config.debug);
    setBool("chime", config.chime);
    setBool("shutdown", config.shutdown);
    setBool("hidden", config.hideConsole);
    setBool("startup-sound", config.startupSound);
    setBool("help", config.help);
    setBool("version", config.version);
    setBool("loggingEnabled", config.loggingEnabled);

    if (result.count("voicemeeter")) {
        config.voicemeeterType.value = result["voicemeeter"].as<uint8_t>();
        config.voicemeeterType.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Voicemeeter type set to: " + std::to_string(config.voicemeeterType.value));
    }
    if (result.count("index")) {
        config.index.value = result["index"].as<uint8_t>();
        config.index.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Index set to: " + std::to_string(config.index.value));
    }
    if (result.count("min")) {
        config.minDbm.value = result["min"].as<int8_t>();
        config.minDbm.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Min dBm set to: " + std::to_string(config.minDbm.value));
    }
    if (result.count("max")) {
        config.maxDbm.value = result["max"].as<int8_t>();
        config.maxDbm.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Max dBm set to: " + std::to_string(config.maxDbm.value));
    }
    if (result.count("polling-interval")) {
        config.pollingEnabled.value = true;
        config.pollingInterval.value = result["polling-interval"].as<uint16_t>();
        config.pollingEnabled.source = ConfigSource::CommandLine;
        config.pollingInterval.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Polling interval set to: " + std::to_string(config.pollingInterval.value) + "ms");
    }
    if (result.count("startup-volume")) {
        config.startupVolumePercent.value = result["startup-volume"].as<int8_t>();
        config.startupVolumePercent.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Startup volume set to: " + std::to_string(config.startupVolumePercent.value) + "%");
    }
    if (result.count("toggle")) {
        config.toggleParam.value = result["toggle"].as<std::string>().c_str();
        config.toggleParam.source = ConfigSource::CommandLine;
        LOG_DEBUG(std::string("[ConfigParser::ApplyCommandLineOptions] Toggle parameter set to: ") + std::string(config.toggleParam.value));
    }
    if (result.count("hotkey-modifiers")) {
        config.hotkeyModifiers.value = result["hotkey-modifiers"].as<uint16_t>();
        config.hotkeyModifiers.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Hotkey modifiers set to: " + config.hotkeyModifiers.value) ;
    }
    if (result.count("hotkey-key")) {
        config.hotkeyVK.value = result["hotkey-key"].as<uint8_t>();
        config.hotkeyVK.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Hotkey key set to: " + std::to_string(config.hotkeyVK.value));
    }
    if (result.count("log")) {
        config.loggingEnabled.value = true;
        config.logFilePath.value = result["log"].as<std::string>().c_str();
        config.loggingEnabled.source = ConfigSource::CommandLine;
        config.logFilePath.source = ConfigSource::CommandLine;
        LOG_DEBUG(std::string("[ConfigParser::ApplyCommandLineOptions] Log file path set to: ") + config.logFilePath.value);
        LOG_DEBUG(std::string("[ConfigParser::ApplyCommandLineOptions] Logging enabled: ") + (config.loggingEnabled.value ? "true" : "false"));
    }
    if (result.count("startup-sound-file")) {
        config.startupSoundFilePath.value = result["startup-sound-file"].as<std::string>().c_str();
        config.startupSoundFilePath.source = ConfigSource::CommandLine;
        LOG_DEBUG(std::string("[ConfigParser::ApplyCommandLineOptions] Startup sound file path set to: ") + std::string(config.startupSoundFilePath.value));
    }
    if (result.count("startup-sound-delay")) {
        config.startupSoundDelay.value = static_cast<uint16_t>(result["startup-sound-delay"].as<int>());
        config.startupSoundDelay.source = ConfigSource::CommandLine;
        LOG_DEBUG("[ConfigParser::ApplyCommandLineOptions] Startup sound delay set to: " + std::to_string(config.startupSoundDelay.value) + "ms");
    }
}

void ConfigParser::LogConfiguration(const Config& config) {
    std::ostringstream oss;
    oss << "Startup Configuration:\n";

    auto logOption = [&](const std::string& name, const std::string& value, ConfigSource source) {
        std::string sourceStr;
        switch (source) {
            case ConfigSource::Default: sourceStr = "[def]"; break;
            case ConfigSource::ConfigFile: sourceStr = "[conf]"; break;
            case ConfigSource::CommandLine: sourceStr = "[cmd]"; break;
        }
        oss << sourceStr << " " << name << ": " << value << "\n";
    };

    logOption("configFilePath", config.configFilePath.value, config.configFilePath.source);
    logOption("logFilePath", config.logFilePath.value, config.logFilePath.source);
    logOption("debug", config.debug.value ? "true" : "false", config.debug.source);
    logOption("loggingEnabled", config.loggingEnabled.value ? "true" : "false", config.loggingEnabled.source);
    logOption("help", config.help.value ? "true" : "false", config.help.source);
    logOption("version", config.version.value ? "true" : "false", config.version.source);
    logOption("hideConsole", config.hideConsole.value ? "true" : "false", config.hideConsole.source);
    logOption("shutdown", config.shutdown.value ? "true" : "false", config.shutdown.source);
    logOption("chime", config.chime.value ? "true" : "false", config.chime.source);
    logOption("pollingEnabled", config.pollingEnabled.value ? "true" : "false", config.pollingEnabled.source);
    logOption("startupSound", config.startupSound.value ? "true" : "false", config.startupSound.source);
    logOption("startupVolumePercent", std::to_string(config.startupVolumePercent.value), config.startupVolumePercent.source);
    logOption("voicemeeterType", std::to_string(config.voicemeeterType.value), config.voicemeeterType.source);
    logOption("index", std::to_string(config.index.value), config.index.source);
    logOption("maxDbm", std::to_string(config.maxDbm.value), config.maxDbm.source);
    logOption("minDbm", std::to_string(config.minDbm.value), config.minDbm.source);
    logOption("monitorDeviceUUID", config.monitorDeviceUUID.value, config.monitorDeviceUUID.source);
    logOption("toggleParam", config.toggleParam.value, config.toggleParam.source);
    logOption("toggleCommand", config.toggleCommand.value ? config.toggleCommand.value : "None", config.toggleCommand.source);
    logOption("pollingInterval", std::to_string(config.pollingInterval.value), config.pollingInterval.source);
    logOption("type", config.type.value, config.type.source);
    logOption("listMonitor", config.listMonitor.value ? "true" : "false", config.listMonitor.source);
    logOption("listInputs", config.listInputs.value ? "true" : "false", config.listInputs.source);
    logOption("listOutputs", config.listOutputs.value ? "true" : "false", config.listOutputs.source);
    logOption("listChannels", config.listChannels.value ? "true" : "false", config.listChannels.source);
    logOption("hotkeyModifiers", std::to_string(config.hotkeyModifiers.value), config.hotkeyModifiers.source);
    logOption("hotkeyVK", std::to_string(config.hotkeyVK.value), config.hotkeyVK.source);

    LOG_DEBUG("[ConfigParser::LogConfiguration] " + oss.str());
}

bool ConfigParser::HandleSpecialCommands(const Config& config) {
    LOG_DEBUG("[ConfigParser::HandleSpecialCommands] Handling special commands");

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
        LOG_DEBUG("[ConfigParser::HandleSpecialCommands] Processing shutdown command");
        RAIIHandle hQuitEvent(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, EVENT_NAME));
        if (hQuitEvent.get()) {
            if (SetEvent(hQuitEvent.get())) {
                LOG_INFO("[ConfigParser::HandleSpecialCommands] Signaled running instances to quit.");
            } else {
                LOG_ERROR("[ConfigParser::HandleSpecialCommands] Failed to signal quit event to running instances.");
            }
        } else {
            LOG_INFO("[ConfigParser::HandleSpecialCommands] No running instances found.");
        }
        return true;
    }

    return false;
}
