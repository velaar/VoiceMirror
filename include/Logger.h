// Logger.h

#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>

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
 * across the application. It runs a separate thread to process log messages
 * asynchronously, ensuring non-blocking behavior.
 */
class Logger {
public:
    /**
     * @brief Get the singleton instance of the Logger.
     * @return Reference to the Logger instance.
     */
    static Logger& Instance();

    /**
     * @brief Initialize the Logger with configuration settings.
     *
     * This method must be called before any logging occurs to ensure that the
     * Logger is properly configured.
     *
     * @param level The desired log level.
     * @param enableFileLogging Whether to enable logging to a file.
     * @param filePath Path to the log file.
     * @return True if initialization was successful, false otherwise.
     */
    bool Initialize(LogLevel level, bool enableFileLogging, const std::string& filePath);

    /**
     * @brief Shutdown the Logger gracefully.
     *
     * This method stops the logging thread and ensures that all pending log
     * messages are processed.
     */
    void Shutdown();

    /**
     * @brief Log a message with the specified log level.
     * @param level The log level of the message.
     * @param message The message to log.
     */
    void Log(LogLevel level, std::string_view message);

    // Delete copy constructor and assignment operator to enforce singleton
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    /**
     * @brief Private constructor for Logger.
     */
    Logger();

    /**
     * @brief Private destructor for Logger.
     */
    ~Logger();

    /**
     * @brief The function run by the logging thread to process log messages.
     */
    void ProcessLogQueue();

    /**
     * @brief Convert LogLevel enum to string representation.
     * @param level The log level.
     * @return String representation of the log level.
     */
    std::string LogLevelToString(LogLevel level);

    LogLevel logLevel;             ///< Current log level.
    std::ofstream logFile;         ///< Output file stream for logging to a file.
    bool fileLoggingEnabled;       ///< Flag indicating if file logging is enabled.
    std::mutex logMutex;           ///< Mutex to protect access to the log queue.
    std::queue<std::pair<LogLevel, std::string>> logQueue; ///< Queue holding pending log messages.
    std::condition_variable cv;    ///< Condition variable to notify the logging thread.
    std::thread loggingThread;     ///< Dedicated thread for processing log messages.
    std::atomic<bool> exitFlag;    ///< Flag to signal the logging thread to exit.
};

#ifdef _DEBUG
    #define LOG_DEBUG(message) Logger::Instance().Log(LogLevel::DEBUG, message)
#else
    #define LOG_DEBUG(message)
#endif

#define LOG_INFO(message) Logger::Instance().Log(LogLevel::INFO, message)
#define LOG_WARNING(message) Logger::Instance().Log(LogLevel::WARNING, message)
#define LOG_ERROR(message) Logger::Instance().Log(LogLevel::ERR, message)
#endif  // LOGGER_H
