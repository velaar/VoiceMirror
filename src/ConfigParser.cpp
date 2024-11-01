
#include "ConfigParser.h"
#include "Logger.h"
#include <fstream>
#include <sstream>

void ConfigParser::ParseConfigFile(const std::string& configPath, std::unordered_map<std::string, std::string>& configMap) {
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        Logger::Instance().Log(LogLevel::INFO, "Config file not found: " + configPath + ". Continuing with command line flags.");
        return;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        // Find the position of the first '#' character
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            // Remove the comment part
            line = line.substr(0, commentPos);
        }

        // Trim leading and trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines after removing comments
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // Trim whitespace from key and value
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            configMap[key] = value;
        }
    }
}

void ConfigParser::ApplyConfig(const std::unordered_map<std::string, std::string>& configMap, Config& config) {
    for (const auto& kv : configMap) {
        const std::string& key = kv.first;
        const std::string& value = kv.second;

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
        } else if (key == "sound") {
            config.sound = (value == "true" || value == "1");
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
        }
    }
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
        ("T,toggle", "Toggle mute between two channels when device is plugged/unplugged. Must use with -m // --monitor Format: type:index1:index2 (e.g., 'input:0:1')", cxxopts::value<std::string>())
        ("V,voicemeeter", "Specify which Voicemeeter to use (1: Voicemeeter, 2: Banana, 3: Potato) (default: 2)", cxxopts::value<int>())
        ("c,config", "Specify a configuration file to manage application parameters.", cxxopts::value<std::string>())
        ("d,debug", "Enable debug mode for extensive logging")
        ("h,help", "Show help message and exit")
        ("i,index", "Specify the Voicemeeter virtual channel index to use (default: 3)", cxxopts::value<int>())
        ("l,log", "Enable logging to a file. Optionally specify a log file path.", cxxopts::value<std::string>())
        ("m,monitor", "Monitor a specific audio device by UUID and restart audio engine on plug/unplug events", cxxopts::value<std::string>())
        ("max", "Maximum dBm for Voicemeeter channel (default: 12.0)", cxxopts::value<float>())
        ("min", "Minimum dBm for Voicemeeter channel (default: -60.0)", cxxopts::value<float>())
        ("p,polling", "Enable polling mode with optional interval in milliseconds (default: 100ms)", cxxopts::value<int>()->implicit_value("100"))
        ("t,type", "Specify the type of channel to use ('input' or 'output') (default: 'input')", cxxopts::value<std::string>())
        ("v,version", "Show program's version number and exit");

    return options;
}

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
        config.type = result["type"].as<std::string>();
    if (result.count("min"))
        config.minDbm = result["min"].as<float>();
    if (result.count("max"))
        config.maxDbm = result["max"].as<float>();
    if (result.count("voicemeeter"))
        config.voicemeeterType = result["voicemeeter"].as<int>();
    if (result.count("debug"))
        config.debug = true;
    if (result.count("chime"))
        config.sound = true;
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
}
