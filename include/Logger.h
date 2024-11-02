#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

/**
 * @brief Enumeration for log levels.
 */
enum class LogLevel {
    DEBUG,    ///< Debug level for detailed internal information.
    INFO,     ///< Informational messages that highlight the progress.
    WARNING,  ///< Potentially harmful situations.
    ERR       ///< Error events that might still allow the application to continue.
};



/**
 * @brief Logger class to handle logging with different levels.
 *
 * The Logger class is implemented as a singleton to ensure consistent logging
 * across the application.
 */
class Logger {
   public:
    /**
     * @brief Get the singleton instance of the Logger.
     * @return Reference to the Logger instance.
     */
    static Logger &Instance();

    /**
     * @brief Set the log level.
     * @param level The desired log level.
     */
    void SetLogLevel(LogLevel level);

    /**
     * @brief Enable logging to a file.
     * @param filePath Path to the log file.
     */
    void EnableFileLogging(const std::string &filePath);

    /**
     * @brief Disable file logging.
     */
    void DisableFileLogging();

    /**
     * @brief Log a message with the specified log level.
     * @param level The log level of the message.
     * @param message The message to log.
     */
    void Log(LogLevel level, std::string_view message);

   private:
    // Private constructor and destructor for singleton pattern
    Logger();
    ~Logger();

    LogLevel logLevel;        ///< Current log level.
    std::ofstream logFile;    ///< Output file stream for logging to a file.
    std::mutex logMutex;      ///< Mutex to protect concurrent access to logging.
    bool fileLoggingEnabled;  ///< Flag indicating if file logging is enabled.
};



#endif  // LOGGER_H

#ifdef _DEBUG
    #define LOG_DEBUG(message) Logger::Instance().Log(LogLevel::DEBUG, message)
#else
    #define LOG_DEBUG(message)
#endif

#define LOG_INFO(message) Logger::Instance().Log(LogLevel::INFO, message)
#define LOG_WARNING(message) Logger::Instance().Log(LogLevel::WARNING, message)
#define LOG_ERROR(message) Logger::Instance().Log(LogLevel::ERR, message)
