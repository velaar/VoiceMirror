#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <windows.h>
#include "Defconf.h"
#include "cxxopts.hpp"

// Struct to hold toggle configuration parameters
struct ToggleConfig {
    std::string type;
    int index1;
    int index2;
};

// Custom deleter for HANDLE to ensure proper resource management
struct HandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

// Alias for a unique_ptr managing HANDLEs with the custom deleter
using UniqueHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;

// Forward declaration of Config structure
struct Config;

// ConfigParser class responsible for parsing and applying configurations
class ConfigParser {
public:
    // Parses the configuration file and populates the config map
    static void ParseConfigFile(const std::string& configPath, std::unordered_map<std::string, std::string>& configMap);

    // Applies the configuration map to the Config structure
    static void ApplyConfig(const std::unordered_map<std::string, std::string>& configMap, Config& config);

    // Validates the parsed command-line options
    static void ValidateOptions(const cxxopts::ParseResult& result);

    // Creates and returns the command-line options
    static cxxopts::Options CreateOptions();

    // Applies the parsed command-line options to the Config structure
    static void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);

    // Handles special commands like help, version, and shutdown
    static bool HandleSpecialCommands(const Config& config);

    // Validates the toggle configuration parameters
    static void ValidateToggleConfig(const Config& config);

    // Sets up logging based on the configuration
    static bool SetupLogging(const Config& config);

    // Handles the overall configuration by parsing the config file and applying settings
    static void HandleConfiguration(const std::string& configPath, Config& config);

    // Parses the toggle parameter into a ToggleConfig structure
    static ToggleConfig ParseToggleParameter(const std::string& toggleParam);
};
