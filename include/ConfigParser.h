#pragma once
#include <unordered_map>
#include <windows.h>
#include "Defconf.h"
#include "cxxopts.hpp"


struct ToggleConfig {
    std::string type;
    int index1;
    int index2;
};

struct HandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using UniqueHandle = std::unique_ptr<std::remove_pointer<HANDLE>::type, HandleDeleter>;
class ConfigParser {
   public:
    static void ParseConfigFile(const std::string& configPath, std::unordered_map<std::string, std::string>& configMap);
    static void ApplyConfig(const std::unordered_map<std::string, std::string>& configMap, Config& config);
    static void ValidateOptions(const cxxopts::ParseResult& result);
    static cxxopts::Options CreateOptions();
    static void ApplyCommandLineOptions(const cxxopts::ParseResult& result, Config& config);
    static bool HandleSpecialCommands(const Config& config);
    static void ValidateToggleConfig(const Config& config);
    static bool SetupLogging(const Config& config);
    static void HandleConfiguration(const std::string& configPath, Config& config);
    static ToggleConfig ParseToggleParameter(const std::string& toggleParam);

};