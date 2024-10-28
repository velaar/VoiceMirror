// VoicemeeterManager.h
#pragma once

#include "VoicemeeterAPI.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <mutex> // Include for std::lock_guard
#include <stdexcept>
#include <string>
#include <cmath>


class VoicemeeterManager {
public:
    VoicemeeterManager();

    bool Initialize(int voicemeeterType);
    
    void Shutdown();

    /**
     * @brief Sends a shutdown command to Voicemeeter.
     */
    void ShutdownCommand();

    /**
     * @brief Restarts the Voicemeeter audio engine.
     * 
     * @param beforeRestartDelay The delay before restarting the audio engine, in seconds (default: 2).
     * @param afterRestartDelay The delay after restarting the audio engine, in seconds (default: 2).
     */
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);

    void SetDebugMode(bool enable);

    
    /**
     * @brief Gets the current debug mode status.
     * 
     * @return True if debug mode is enabled, false otherwise.
     */
    bool GetDebugMode();
    VoicemeeterAPI& GetAPI();

private:
    VoicemeeterAPI vmrAPI;
    bool loggedIn;
    bool debugMode;
    std::mutex toggleMutex; // Mutex for thread-safe operations

    void DebugMessage(const std::string& message);
};
