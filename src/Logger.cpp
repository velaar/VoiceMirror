#include "Logger.h"
#include <sstream>
#include <windows.h>

// Include Defconf.h for color definitions
#include "Defconf.h"

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : logLevel(LogLevel::INFO),
      fileLoggingEnabled(false),
      consoleHandle(GetStdHandle(STD_OUTPUT_HANDLE)) { 
    if (consoleHandle.get() == INVALID_HANDLE_VALUE) {
        std::cerr << "Logger: Failed to obtain console handle." << std::endl;
    }
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

    return true;
}

void Logger::Shutdown() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::Log(LogLevel level, std::string_view message) {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(logLevel)) {
        return;
    }

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
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_tm);

    // Prepare the log message
    std::ostringstream logStream;
    logStream << "[" << time_buffer << "." << std::setfill('0') << std::setw(3)
              << milliseconds.count() << "] " << LogLevelToString(level) << ": " << message << std::endl;

    std::string finalLog = logStream.str();

    // Output the log message with colorization
    if (fileLoggingEnabled && logFile.is_open()) {
        logFile << finalLog;
        logFile.flush(); // Ensure the message is written immediately
    } else {
        // Set console text color based on log level
        WORD originalAttributes;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(consoleHandle.get(), &csbi)) {
            originalAttributes = csbi.wAttributes;
        } else {
            originalAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Default to white
        }

        // Set color
        SetConsoleTextAttribute(consoleHandle.get(), GetColorForLogLevel(level));

        // Write to console
        std::cout << finalLog;

        // Reset to original color
        SetConsoleTextAttribute(consoleHandle.get(), originalAttributes);
    }
}

constexpr const char* Logger::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERR: return "ERROR";
        default: return "UNKNOWN";
    }
}

WORD Logger::GetColorForLogLevel(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG:
            return DEBUG_COLOR;
        case LogLevel::INFO:
            return INFO_COLOR;
        case LogLevel::WARNING:
            return WARNING_COLOR;
        case LogLevel::ERR:
            return ERROR_COLOR;
        default:
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Default to white
    }
}
