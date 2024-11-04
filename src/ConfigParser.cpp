#include "ConfigParser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <iomanip>

#include "VolumeUtils.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "Defconf.h"

// Include Windows headers only if necessary
#include <windows.h>

// Include cxxopts for command-line option parsing
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
    LOG_DEBUG("Parsing toggle parameter: " + toggleParam);
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

    LOG_DEBUG("Parsed ToggleConfig - Type: " + toggleConfig.type +
              ", Index1: " + std::to_string(toggleConfig.index1) +
              ", Index2: " + std::to_string(toggleConfig.index2));

    return toggleConfig;
}

bool ConfigParser::SetupLogging(const Config& config) {
    LogLevel level = config.debug ? LogLevel::DEBUG : LogLevel::INFO;
    bool enableFileLogging = config.loggingEnabled;
    const std::string& filePath = config.logFilePath;

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

void ConfigParser::HandleConfiguration(const std::string& configPath, Config& config) {
    LOG_DEBUG("Starting configuration handling.");

    ParseConfigFile(configPath, config);

    cxxopts::Options options = CreateOptions();
    cxxopts::ParseResult result;
    try {
        result = options.parse(argc_, argv_);
    } catch (const cxxopts::exceptions::parsing& e) {
        LOG_ERROR("Error parsing command-line options: " + std::string(e.what()));
        throw;
    }

    ValidateOptions(result);

    ApplyCommandLineOptions(result, config);

    if (!SetupLogging(config)) {
        throw std::runtime_error("Failed to setup logging.");
    }

    if (HandleSpecialCommands(config)) {
        exit(0);
    }

    LOG_DEBUG("Configuration handling completed.");
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

            if (key == "list-monitor") {
                config.listMonitor = (value == "true" || value == "1");
            } else if (key == "list-inputs") {
                config.listInputs = (value == "true" || value == "1");
            } else if (key == "list-outputs") {
                config.listOutputs = (value == "true" || value == "1");
            } else if (key == "list-channels") {
                config.listChannels = (value == "true" || value == "1");
            } else if (key == "index") {
                config.index = std::stoi(value);
            } else if (key == "type") {
                config.type = value;
            } else if (key == "min") {
                config.minDbm = std::stof(value);
            } else if (key == "max") {
                config.maxDbm = std::stof(value);
            } else if (key == "voicemeeter") {
                config.voicemeeterType = std::stoi(value);
            } else if (key == "debug") {
                config.debug = (value == "true" || value == "1");
            } else if (key == "chime") {
                config.chime = (value == "true" || value == "1");
            } else if (key == "monitor") {
                config.monitorDeviceUUID = value;
            } else if (key == "log") {
                config.loggingEnabled = true;
                config.logFilePath = value;
            } else if (key == "hidden") {
                config.hideConsole = (value == "true" || value == "1");
            } else if (key == "toggle") {
                config.toggleParam = value;
            } else if (key == "shutdown") {
                config.shutdown = (value == "true" || value == "1");
            } else if (key == "polling-interval") {
                config.pollingEnabled = true;
                config.pollingInterval = std::stoi(value);
            } else if (key == "startup-volume") {
                config.startupVolumePercent = std::stoi(value);
            } else if (key == "startup-sound") {
                config.startupSound = (value == "true" || value == "1");
            } else {
                LOG_DEBUG("Unknown config key: " + key);
            }
        }
    }
    LOG_DEBUG("Finished parsing config file");
}

void ConfigParser::ValidateOptions(const cxxopts::ParseResult& result) {
    LOG_DEBUG("Validating command line options");

    if (result.count("index")) {
        int index = result["index"].as<int>();
        LOG_DEBUG("Validating index: " + std::to_string(index));
        if (index < 0) {
            throw std::runtime_error("Channel index must be non-negative");
        }
    }
    if (result.count("voicemeeter")) {
        int type = result["voicemeeter"].as<int>();
        LOG_DEBUG("Validating voicemeeter type: " + std::to_string(type));
        if (type < VOICEMEETER_BASIC || type > VOICEMEETER_POTATO) {
            throw std::runtime_error("Voicemeeter type must be " +
                                     std::to_string(VOICEMEETER_BASIC) + " (Basic), " +
                                     std::to_string(VOICEMEETER_BANANA) + " (Banana), or " +
                                     std::to_string(VOICEMEETER_POTATO) + " (Potato)");
        }
    }

    if (result.count("type")) {
        std::string type = Trim(result["type"].as<std::string>());
        LOG_DEBUG("Validating channel type: " + type);
        if (type != "input" && type != "output") {
            throw std::runtime_error("Type must be either 'input' or 'output'");
        }
    }

    if (result.count("polling-interval")) {
        int interval = result["polling-interval"].as<int>();
        LOG_DEBUG("Validating polling interval: " + std::to_string(interval));
        if (interval < 10 || interval > 1000) {
            throw std::runtime_error("Polling interval must be between 10 and 1000 milliseconds");
        }
    }

    LOG_DEBUG("Command line options validation complete");
}

cxxopts::Options ConfigParser::CreateOptions() {
    LOG_DEBUG("Creating command line options");

    cxxopts::Options options("VoiceMirror", "Synchronize Windows Volume with Voicemeeter virtual channels");

    options.add_options()
        ("C,chime", "Enable chime sound on sync from Voicemeeter to Windows")
        ("L,list-channels", "List all Voicemeeter channels with their labels and exit")
        ("S,shutdown", "Shutdown all instances of the app and exit immediately")
        ("H,hidden", "Hide the console window. Use with --log to run without showing the console.")
        ("I,list-inputs", "List available Voicemeeter virtual inputs and exit")
        ("M,list-monitor", "List monitorable audio devices and exit")
        ("O,list-outputs", "List available Voicemeeter virtual outputs and exit")
        ("V,voicemeeter", "Specify which Voicemeeter to use (1: Basic, 2: Banana, 3: Potato)", cxxopts::value<int>()->default_value(std::to_string(DEFAULT_VOICEMEETER_TYPE)))
        ("i,index", "Specify the Voicemeeter virtual channel index to use", cxxopts::value<int>()->default_value(std::to_string(DEFAULT_CHANNEL_INDEX)))
        ("min", "Minimum dBm for Voicemeeter channel", cxxopts::value<float>()->default_value(std::to_string(DEFAULT_MIN_DBM)))
        ("max", "Maximum dBm for Voicemeeter channel", cxxopts::value<float>()->default_value(std::to_string(DEFAULT_MAX_DBM)))
        ("p,polling-interval", "Enable polling mode with interval in milliseconds", cxxopts::value<int>()->default_value(std::to_string(DEFAULT_POLLING_INTERVAL_MS)))
        ("s,startup-volume", "Set the initial Windows volume level as a percentage (0-100)", cxxopts::value<int>()->default_value(std::to_string(DEFAULT_STARTUP_VOLUME_PERCENT)))
        ("T,toggle", "Toggle parameter", cxxopts::value<std::string>()->default_value(DEFAULT_TOGGLE_COMMAND))
        ("d,debug", "Enable debug logging mode");

    options.add_options("Advanced")
        ("m,monitor", "Specify the monitor device UUID", cxxopts::value<std::string>()->default_value(DEFAULT_MONITOR_DEVICE_UUID))
        ("log", "Enable logging with specified log file path", cxxopts::value<std::string>()->default_value(DEFAULT_LOG_FILE))
        ("startup-sound", "Enable startup sound", cxxopts::value<bool>()->default_value("false"));

    options.add_options("Help")
        ("help", "Print help")
        ("version", "Print version");

    LOG_DEBUG("Command line options created");
    return options;
}

void ConfigParser::ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config) {
    LOG_DEBUG("Applying command line options to config");

    if (result.count("list-monitor")) {
        config.listMonitor = true;
        LOG_DEBUG("Setting list-monitor: true");
    }
    if (result.count("list-inputs")) {
        config.listInputs = true;
        LOG_DEBUG("Setting list-inputs: true");
    }
    if (result.count("list-outputs")) {
        config.listOutputs = true;
        LOG_DEBUG("Setting list-outputs: true");
    }
    if (result.count("list-channels")) {
        config.listChannels = true;
        LOG_DEBUG("Setting list-channels: true");
    }
    if (result.count("index")) {
        config.index = result["index"].as<int>();
        LOG_DEBUG("Setting index: " + std::to_string(config.index));
    }
    if (result.count("type")) {
        config.type = Trim(result["type"].as<std::string>());
        LOG_DEBUG("Setting type: " + config.type);
    }
    if (result.count("min")) {
        config.minDbm = result["min"].as<float>();
        LOG_DEBUG("Setting min dBm: " + std::to_string(config.minDbm));
    }
    if (result.count("max")) {
        config.maxDbm = result["max"].as<float>();
        LOG_DEBUG("Setting max dBm: " + std::to_string(config.maxDbm));
    }
    if (result.count("voicemeeter")) {
        config.voicemeeterType = result["voicemeeter"].as<int>();
        LOG_DEBUG("Setting voicemeeter type: " + std::to_string(config.voicemeeterType));
    }
    if (result.count("debug")) {
        config.debug = true;
        LOG_DEBUG("Setting debug mode: true");
    }
    if (result.count("chime")) {
        config.chime = true;
        LOG_DEBUG("Setting chime: true");
    }
    if (result.count("monitor")) {
        config.monitorDeviceUUID = result["monitor"].as<std::string>();
        LOG_DEBUG("Setting monitor device UUID: " + config.monitorDeviceUUID);
    }
    if (result.count("log")) {
        config.loggingEnabled = true;
        config.logFilePath = result["log"].as<std::string>();
        LOG_DEBUG("Setting logging enabled with path: " + config.logFilePath);
    }
    if (result.count("hidden")) {
        config.hideConsole = true;
        LOG_DEBUG("Setting hide console: true");
    }
    if (result.count("toggle")) {
        config.toggleParam = result["toggle"].as<std::string>();
        LOG_DEBUG("Setting toggle parameter: " + config.toggleParam);
    }
    if (result.count("shutdown")) {
        config.shutdown = true;
        LOG_DEBUG("Setting shutdown: true");
    }
    if (result.count("help")) {
        config.help = true;
        LOG_DEBUG("Setting help: true");
    }
    if (result.count("version")) {
        config.version = true;
        LOG_DEBUG("Setting version: true");
    }
    if (result.count("polling-interval")) {
        config.pollingEnabled = true;
        config.pollingInterval = result["polling-interval"].as<int>();
        LOG_DEBUG("Setting polling enabled with interval: " + std::to_string(config.pollingInterval));
    }
    if (result.count("startup-volume")) {
        config.startupVolumePercent = result["startup-volume"].as<int>();
        LOG_DEBUG("Setting startup volume: " + std::to_string(config.startupVolumePercent));
    }
    if (result.count("startup-sound")) {
        config.startupSound = result["startup-sound"].as<bool>();
        LOG_DEBUG("Setting startup sound: " + std::to_string(config.startupSound));
    }
    if (result.count("toggle-command")) {
        config.toggleCommand = result["toggle-command"].as<std::string>();
        LOG_DEBUG("Setting toggle command: " + config.toggleCommand);
    }

    LOG_DEBUG("Finished applying command line options");
}

bool ConfigParser::HandleSpecialCommands(const Config& config) {
    LOG_DEBUG("Handling special commands");

    if (config.help) {
        cxxopts::Options options = CreateOptions();
        std::cout << options.help() << std::endl;
        return true;
    }

    if (config.version) {
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

    if (config.shutdown) {
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
