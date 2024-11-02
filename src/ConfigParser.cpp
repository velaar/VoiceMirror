// ConfigParser.cpp

#include "ConfigParser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "VolumeUtils.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "Defconf.h"

// Include Windows headers only if necessary
#include <windows.h>

// Include cxxopts if needed for command-line options (though likely not here)
#include "cxxopts.hpp"

/**
 * @brief Parse the configuration file and populate the Config structure directly.
 */
void ConfigParser::ParseConfigFile(const std::string& configPath, Config& config) {
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        LOG_INFO("Config file not found: " + configPath + ". Continuing with command line flags.");
        return;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            // Directly assign to Config structure
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
            } else if (key == "polling") {
                config.pollingEnabled = true;
                config.pollingInterval = std::stoi(value);
            } else if (key == "startup_volume") {
                config.startupVolumePercent = std::stoi(value);
            } else if (key == "startup_sound") {
                config.startupSound = (value == "true" || value == "1");
            }
            // Add additional configuration keys as necessary
        }
    }
}

/**
 * @brief Apply the configuration map to the Config structure.
 * (Removed since configMap is no longer used)
 * This function is no longer necessary and can be removed.
 */

/**
 * @brief Validate command-line options.
 */
void ConfigParser::ValidateOptions(const cxxopts::ParseResult& result) {
    if (result.count("index")) {
        int index = result["index"].as<int>();
        if (index < 0) {
            throw std::runtime_error("Channel index must be non-negative");
        }
    }
    if (result.count("startup-volume")) {
        int vol = result["startup-volume"].as<int>();
        if (vol < 0 || vol > 100) {
            throw std::runtime_error("Startup volume must be between 0 and 100.");
        }
    }

    if (result.count("voicemeeter")) {
        int type = result["voicemeeter"].as<int>();
        if (type < 1 || type > 3) {
            throw std::runtime_error("Voicemeeter type must be 1 (Standard), 2 (Banana), or 3 (Potato)");
        }
    }

    if (result.count("type")) {
        std::string type = result["type"].as<std::string>();
        if (type != "input" && type != "output") {
            throw std::runtime_error("Type must be either 'input' or 'output'");
        }
    }

    if (result.count("polling")) {
        int interval = result["polling"].as<int>();
        if (interval < 10 || interval > 1000) {
            throw std::runtime_error("Polling interval must be between 10 and 1000 milliseconds");
        }
    }
}

/**
 * @brief Create and return the cxxopts::Options object.
 */
cxxopts::Options ConfigParser::CreateOptions() {
    cxxopts::Options options("VoiceMirror", "Synchronize Windows Volume with Voicemeeter virtual channels");

    options.add_options()
        ("C,chime", "Enable chime sound on sync from Voicemeeter to Windows")
        ("L,list-channels", "List all Voicemeeter channels with their labels and exit")
        ("S,shutdown", "Shutdown all instances of the app and exit immediately")
        ("H,hidden", "Hide the console window. Use with --log to run without showing the console.")
        ("I,list-inputs", "List available Voicemeeter virtual inputs and exit")
        ("M,list-monitor", "List monitorable audio devices and exit") //defunct
        ("O,list-outputs", "List available Voicemeeter virtual outputs and exit")
        ("T,toggle", "Toggle mute between two channels when device is plugged/unplugged. Must use with -m // --monitor Format: type:index1:index2 (e.g., 'input:0:1')", cxxopts::value<std::string>())
        ("V,voicemeeter", "Specify which Voicemeeter to use (1: Voicemeeter, 2: Banana, 3: Potato) (default: 2)", cxxopts::value<int>()->default_value("2"))
        ("c,config", "Specify a configuration file to manage application parameters.", cxxopts::value<std::string>())
        ("d,debug", "Enable debug mode for extensive logging")
        ("h,help", "Show help message and exit")
        ("i,index", "Specify the Voicemeeter virtual channel index to use (default: 3)", cxxopts::value<int>()->default_value("3"))
        ("l,log", "Enable logging to a file. Optionally specify a log file path.", cxxopts::value<std::string>()->default_value("VoiceMirror.log"))
        ("m,monitor", "Monitor a specific audio device by UUID and restart audio engine on plug/unplug events", cxxopts::value<std::string>())
        ("max", "Maximum dBm for Voicemeeter channel (default: 12.0)", cxxopts::value<float>()->default_value("12.0"))
        ("min", "Minimum dBm for Voicemeeter channel (default: -60.0)", cxxopts::value<float>()->default_value("-60.0"))
        ("p,polling", "Enable polling mode with optional interval in milliseconds (default: 100ms)", cxxopts::value<int>()->default_value("100"))
        ("t,type", "Specify the type of channel to use ('input' or 'output') (default: 'input')", cxxopts::value<std::string>()->default_value("input"))
        ("v,version", "Show program's version number and exit")
        ("s,startup-volume", "Set the initial Windows volume level as a percentage (0-100)", cxxopts::value<int>())
        ("u,startup-sound", "Play a startup sound after setting the initial volume", cxxopts::value<bool>()->default_value("false"));

    return options;
}

/**
 * @brief Apply command-line options directly to the Config structure.
 */
void ConfigParser::ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config) {
    if (result.count("list-monitor"))
        config.listMonitor = true;
    if (result.count("list-inputs"))
        config.listInputs = true;
    if (result.count("list-outputs"))
        config.listOutputs = true;
    if (result.count("list-channels"))
        config.listChannels = true;
    if (result.count("index"))
        config.index = result["index"].as<int>();
    if (result.count("type"))
        config.type = VolumeUtils::Trim(result["type"].as<std::string>());
    if (result.count("min"))
        config.minDbm = result["min"].as<float>();
    if (result.count("max"))
        config.maxDbm = result["max"].as<float>();
    if (result.count("voicemeeter"))
        config.voicemeeterType = result["voicemeeter"].as<int>();
    if (result.count("debug"))
        config.debug = true;
    if (result.count("chime"))
        config.chime = true;
    if (result.count("monitor"))
        config.monitorDeviceUUID = result["monitor"].as<std::string>();
    if (result.count("log")) {
        config.loggingEnabled = true;
        config.logFilePath = result["log"].as<std::string>();
    }
    if (result.count("hidden"))
        config.hideConsole = true;
    if (result.count("toggle"))
        config.toggleParam = result["toggle"].as<std::string>();
    if (result.count("shutdown"))
        config.shutdown = true;
    if (result.count("help"))
        config.help = true;
    if (result.count("version"))
        config.version = true;
    if (result.count("polling")) {
        config.pollingEnabled = true;
        config.pollingInterval = result["polling"].as<int>();
    }
    if (result.count("startup-volume")) {
        config.startupVolumePercent = result["startup-volume"].as<int>();
    }
    if (result.count("startup-sound")) {
        config.startupSound = result["startup-sound"].as<bool>();
    }
}

/**
 * @brief Handle special commands like --help, --version, --shutdown.
 */
bool ConfigParser::HandleSpecialCommands(const Config& config) {
    if (config.help) {
        // Typically, you would print the help message here using cxxopts.
        // For demonstration, we're logging an info message.
        LOG_INFO("Use --help to see available options.");
        return true;
    }

    if (config.version) {
        LOG_INFO("VoiceMirror Version 0.2.0-alpha");
        return true;
    }

    if (config.shutdown) {
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

/**
 * @brief Handle the overall configuration parsing.
 */
void ConfigParser::HandleConfiguration(const std::string& configPath, Config& config) {
    // Directly parse into Config structure
    ParseConfigFile(configPath, config);
    // Command-line options should already be applied before calling this function
    // Alternatively, you can pass the parsed command-line options here as well
}

/**
 * @brief Parse the toggle parameter.
 */
ToggleConfig ConfigParser::ParseToggleParameter(const std::string& toggleParam) {
    ToggleConfig config;
    size_t firstColon = toggleParam.find(':');
    size_t secondColon = toggleParam.find(':', firstColon + 1);

    if (firstColon == std::string::npos || secondColon == std::string::npos) {
        throw std::runtime_error("Invalid toggle parameter format. Expected format: type:index1:index2");
    }

    config.type = toggleParam.substr(0, firstColon);
    try {
        config.index1 = std::stoi(toggleParam.substr(firstColon + 1, secondColon - firstColon - 1));
        config.index2 = std::stoi(toggleParam.substr(secondColon + 1));
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid indices in toggle parameter: " + std::string(e.what()));
    }

    return config;
}

/**
 * @brief Setup the Logger based on the configuration.
 */
bool ConfigParser::SetupLogging(const Config& config) {
    LogLevel level = LogLevel::INFO;
    if (config.debug) {
        level = LogLevel::DEBUG;
    }

    bool enableFileLogging = config.loggingEnabled;
    std::string filePath = config.logFilePath;

    if (!Logger::Instance().Initialize(level, enableFileLogging, filePath)) {
        std::cerr << "Failed to initialize Logger. Exiting application." << std::endl;
        return false;
    }

    if (config.hideConsole && !config.loggingEnabled) {
        LOG_ERROR("--hidden option requires --log to be specified.");
        return false;
    }

    return true;
}
