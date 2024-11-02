// Logger.cpp

#include "Logger.h"
#include <sstream>

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : logLevel(LogLevel::INFO),
      fileLoggingEnabled(false),
      exitFlag(false) {
    // Constructor intentionally left blank
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(LogLevel level, bool enableFileLogging, const std::string& filePath) {
    logLevel = level;
    fileLoggingEnabled = enableFileLogging;

    if (fileLoggingEnabled) {
        logFile.open(filePath, std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Logger: Failed to open log file: " << filePath << std::endl;
            fileLoggingEnabled = false;
            return false;
        }
    }

    // Start the logging thread
    try {
        loggingThread = std::thread(&Logger::ProcessLogQueue, this);
    } catch (const std::exception& ex) {
        std::cerr << "Logger: Failed to start logging thread: " << ex.what() << std::endl;
        if (logFile.is_open()) {
            logFile.close();
        }
        return false;
    }

    return true;
}

void Logger::Shutdown() {
    if (!exitFlag.exchange(true)) { // Ensure shutdown is only performed once
        cv.notify_all();
        if (loggingThread.joinable()) {
            loggingThread.join();
        }
        if (logFile.is_open()) {
            logFile.close();
        }
    }
}

void Logger::Log(LogLevel level, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(logLevel)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(logMutex);
        logQueue.emplace(level, std::string(message));
    }
    cv.notify_one();
}

std::string Logger::LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "<DEBUG> ";
        case LogLevel::INFO:
            return "<INFO> ";
        case LogLevel::WARNING:
            return "<WARNING> ";
        case LogLevel::ERR:
            return "<ERROR> ";
        default:
            return "<UNKNOWN> ";
    }
}

void Logger::ProcessLogQueue() {
    while (!exitFlag.load() || !logQueue.empty()) {
        std::unique_lock<std::mutex> lock(logMutex);
        cv.wait(lock, [this]() { return exitFlag.load() || !logQueue.empty(); });

        while (!logQueue.empty()) {
            auto [level, message] = logQueue.front();
            logQueue.pop();
            lock.unlock(); // Unlock while processing the message

            // Get current time
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm local_tm;
#ifdef _WIN32
            localtime_s(&local_tm, &now_time_t);
#else
            localtime_r(&now_time_t, &local_tm);
#endif
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now.time_since_epoch()) % 1000;

            // Format time
            char time_buffer[64];
            std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
                          &local_tm);

            // Prepare the log message
            std::ostringstream logStream;
            logStream << "[" << time_buffer << "." << std::setfill('0') << std::setw(3)
                      << milliseconds.count() << "] " << LogLevelToString(level) << message << std::endl;

            // Output the log message
            if (fileLoggingEnabled && logFile.is_open()) {
                logFile << logStream.str();
                logFile.flush(); // Ensure the message is written immediately
            } else {
                std::cout << logStream.str();
            }

            lock.lock(); // Re-lock to check for more messages
        }
    }
}
