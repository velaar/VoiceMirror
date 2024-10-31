#include "Logger.h"

/**
 * @brief Get the singleton instance of the Logger.
 * @return Reference to the Logger instance.
 */
Logger &Logger::Instance()
{
    static Logger instance;
    return instance;
}

/**
 * @brief Private constructor for Logger.
 */
Logger::Logger()
    : logLevel(LogLevel::INFO), fileLoggingEnabled(false)
{
}

/**
 * @brief Destructor for Logger.
 */
Logger::~Logger()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

/**
 * @brief Set the log level.
 * @param level The desired log level.
 */
void Logger::SetLogLevel(LogLevel level)
{
    logLevel = level;
}

/**
 * @brief Enable logging to a file.
 * @param filePath Path to the log file.
 */
void Logger::EnableFileLogging(const std::string &filePath)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
    {
        logFile.close();
    }
    logFile.open(filePath, std::ios::out | std::ios::app);
    fileLoggingEnabled = logFile.is_open();
}

/**
 * @brief Disable file logging.
 */
void Logger::DisableFileLogging()
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
    {
        logFile.close();
    }
    fileLoggingEnabled = false;
}

/**
 * @brief Log a message with the specified log level.
 * @param level The log level of the message.
 * @param message The message to log.
 */
void Logger::Log(LogLevel level, const std::string &message)
{
    if (static_cast<int>(level) < static_cast<int>(logLevel))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex);

    std::ostream &output = (fileLoggingEnabled && logFile.is_open()) ? logFile : std::cout;

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_time_t);
#else
    localtime_r(&now_time_t, &local_tm);
#endif
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    // Format time
    char time_buffer[64];
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_tm);

    // Log level as string
    std::string levelStr;
    switch (level)
    {
    case LogLevel::DEBUG:
        levelStr = "<DEBUG> ";
        break;
    case LogLevel::INFO:
        levelStr = "<INFO> ";
        break;
    case LogLevel::WARNING:
        levelStr = "<WARNING> ";
        break;
    case LogLevel::ERR:
        levelStr = "<ERROR> ";
        break;
    default:
        levelStr = "<UNKNOWN> ";
        break;
    }

    // Output the log message
    output << "[" << time_buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count() << "] " << levelStr << message << std::endl;
}
