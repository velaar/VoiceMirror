// ConfigParser.h

#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <string>
#include "Logger.h"
#include "cxxopts.hpp"
#include "Defconf.h"

/**
 * @brief Structure to hold toggle configuration parameters.
 */
struct ToggleConfig {
    std::string type; ///< Type of channel ('input' or 'output').
    int index1;       ///< First channel index.
    int index2;       ///< Second channel index.
};

/**
 * @brief Class responsible for parsing and validating configuration.
 */
class ConfigParser {
public:
    /**
     * @brief Parse the configuration file and populate the Config structure directly.
     * @param configPath Path to the configuration file.
     * @param config Reference to the Config structure to populate.
     */
    static void ParseConfigFile(const std::string& configPath, Config& config);

    /**
     * @brief Validate command-line options.
     * @param result The parsed command-line options.
     */
    static void ValidateOptions(const cxxopts::ParseResult& result);

    /**
     * @brief Create and return the cxxopts::Options object.
     * @return Configured cxxopts::Options object.
     */
    static cxxopts::Options CreateOptions();

    /**
     * @brief Apply command-line options directly to the Config structure.
     * @param result The parsed command-line options.
     * @param config Reference to the Config structure to populate.
     */
    static void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);

    /**
     * @brief Handle special commands like --help, --version, --shutdown.
     * @param config The current configuration.
     * @return True if a special command was handled and the program should exit, false otherwise.
     */
    static bool HandleSpecialCommands(const Config& config);

    /**
     * @brief Handle the overall configuration parsing.
     * @param configPath Path to the configuration file.
     * @param config Reference to the Config structure to populate.
     */
    static void HandleConfiguration(const std::string& configPath, Config& config);

    /**
     * @brief Parse the toggle parameter.
     * @param toggleParam The toggle parameter string.
     * @return Parsed ToggleConfig structure.
     */
    static ToggleConfig ParseToggleParameter(const std::string& toggleParam);

    /**
     * @brief Setup the Logger based on the configuration.
     * @param config The current configuration.
     * @return True if Logger setup was successful, false otherwise.
     */
    static bool SetupLogging(const Config& config);
};

#endif // CONFIGPARSER_H
