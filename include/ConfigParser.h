// ConfigParser.h

#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <string>
#include "Logger.h"
#include "cxxopts.hpp"
#include "Defconf.h"


class ConfigParser {
public:
    ConfigParser(int argc, char** argv);
    void HandleConfiguration(Config& config);
    static ToggleConfig ParseToggleParameter(const std::string& toggleParam);

private:
    static std::string Trim(const std::string& str);
    void ParseConfigFile(const std::string& configPath, Config& config);
    void ValidateOptions(const cxxopts::ParseResult& result);
    cxxopts::Options CreateOptions();
    void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);
    bool HandleSpecialCommands(const Config& config);
    bool SetupLogging(const Config& config);
    void LogConfiguration(const Config& config);
    void ValidateConfig(const Config& config);

    int argc_;
    char** argv_;
};
#endif // CONFIGPARSER_H
