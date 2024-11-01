#pragma once
#include "Defconf.h"
#include "cxxopts.hpp"
#include <unordered_map>

/**
 * @brief Provides methods for parsing and applying configuration settings.
 *
 * The `ConfigParser` class is responsible for parsing configuration files, applying the parsed settings to a `Config` object, and validating command-line options.
 *
 * @see `Defconf.h` for the definition of the `Config` struct.
 * @see `cxxopts.hpp` for the command-line parsing library used.
 */
class ConfigParser {
public:
    /**
     * @brief Parses a configuration file and stores the key-value pairs in a `configMap`.
     *
     * @param configPath The path to the configuration file.
     * @param configMap A reference to the `std::unordered_map` to store the parsed configuration.
     */
    static void ParseConfigFile(const std::string& configPath, std::unordered_map<std::string, std::string>& configMap);

    /**
     * @brief Applies the configuration settings from the `configMap` to the provided `Config` object.
     *
     * @param configMap A reference to the `std::unordered_map` containing the configuration settings.
     * @param config A reference to the `Config` object to apply the settings to.
     */
    static void ApplyConfig(const std::unordered_map<std::string, std::string>& configMap, Config& config);

    /**
     * @brief Validates the command-line options parsed by `cxxopts`.
     *
     * @param result The `cxxopts::ParseResult` containing the parsed command-line options.
     */
    static void ValidateOptions(const cxxopts::ParseResult& result);

    /**
     * @brief Creates the `cxxopts::Options` object for parsing command-line arguments.
     *
     * @return The `cxxopts::Options` object.
     */
    static cxxopts::Options CreateOptions();

    /**
     * @brief Applies the command-line options to the provided `Config` object.
     *
     * @param result The `cxxopts::ParseResult` containing the parsed command-line options.
     * @param config A reference to the `Config` object to apply the options to.
     */
    static void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);
};
