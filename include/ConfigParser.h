// ConfigParser.h

#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <string>
#include "Logger.h"
#include "cxxopts.hpp"
#include "Defconf.h"
#include "VoicemeeterManager.h"

/**
 * @brief Class responsible for parsing and validating configuration.
 */
class ConfigParser {
public:
    /**
     * @brief Constructor to initialize command-line arguments.
     * @param argc Argument count.
     * @param argv Argument vector.
     */
    ConfigParser(int argc, char** argv);

    /**
     * @brief Parse the configuration file and populate the Config structure directly.
     * @param configPath Path to the configuration file.
     * @param config Reference to the Config structure to populate.
     */
    void ParseConfigFile(const std::string& configPath, Config& config);

    /**
     * @brief Validate command-line options.
     * @param result The parsed command-line options.
     */
    void ValidateOptions(const cxxopts::ParseResult& result);

    /**
     * @brief Create and return the cxxopts::Options object.
     * @return Configured cxxopts::Options object.
     */
    cxxopts::Options CreateOptions();

    /**
     * @brief Apply command-line options directly to the Config structure.
     * @param result The parsed command-line options.
     * @param config Reference to the Config structure to populate.
     */
    void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);

    /**
     * @brief Handle special commands like --help, --version, --shutdown.
     * @param config The current configuration.
     * @return True if a special command was handled and the program should exit, false otherwise.
     */
    bool HandleSpecialCommands(const Config& config);

    /**
     * @brief Handle the overall configuration parsing.
     * @param configPath Path to the configuration file.
     * @param config Reference to the Config structure to populate.
     */
    void HandleConfiguration(const std::string& configPath, Config& config);

    /**
     * @brief Parse the toggle parameter.
     * @param toggleParam The toggle parameter string.
     * @return Parsed ToggleConfig structure.
     */
    ToggleConfig ParseToggleParameter(const std::string& toggleParam);

    /**
     * @brief Setup the Logger based on the configuration.
     * @param config The current configuration.
     * @return True if Logger setup was successful, false otherwise.
     */
    bool SetupLogging(const Config& config);



private:
    /**
     * @brief Trim whitespace from both ends of a string.
     * @param str The string to trim.
     * @return A new trimmed string.
     */
    static std::string Trim(const std::string& str);

    int argc_;         ///< Argument count
    char** argv_;      ///< Argument vector

        /**
     * @brief List all monitorable audio devices.
     */
    void ListMonitorableDevices();

    /**
     * @brief List all available Voicemeeter virtual inputs.
     * @param vmManager Reference to the VoicemeeterManager instance.
     */
    void ListInputs(VoicemeeterManager& vmManager);

    /**
     * @brief List all available Voicemeeter virtual outputs.
     * @param vmManager Reference to the VoicemeeterManager instance.
     */
    void ListOutputs(VoicemeeterManager& vmManager);
};

#endif // CONFIGPARSER_H