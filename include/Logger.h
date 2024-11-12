#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <atomic>
#include <string_view>
#include "RAIIHandle.h"
#include "Defconf.h"

/**
 * @brief Logger class to handle logging with different levels.
 *
 * The Logger class is implemented as a singleton to ensure consistent logging
 * across the application.
 */
class Logger {
public:
    static Logger& Instance();

    bool Initialize(LogLevel level, bool enableFileLogging, const std::string& filePath);
    void Shutdown();
    void Log(LogLevel level, std::string_view message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

     char buffer_[512]; // Reduced buffer size

    constexpr const char* LogLevelToString(LogLevel level) const;
    WORD GetColorForLogLevel(LogLevel level) const;

    LogLevel logLevel;
    std::ofstream logFile;
    bool fileLoggingEnabled;
    RAIIHandle consoleHandle;
};

#ifdef _DEBUG
    #define LOG_DEBUG(message) Logger::Instance().Log(LogLevel::DEBUG, message)
#else
    #define LOG_DEBUG(message)
#endif

#define LOG_INFO(message) Logger::Instance().Log(LogLevel::INFO, message)
#define LOG_WARNING(message) Logger::Instance().Log(LogLevel::WARNING, message)
#define LOG_ERROR(message) Logger::Instance().Log(LogLevel::ERR, message)

