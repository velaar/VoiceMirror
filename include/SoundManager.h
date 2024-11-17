// SoundManager.h
#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Logger.h"

class SoundManager {
   public:
    // Singleton access
    static SoundManager& Instance();

    // Deleted methods to enforce singleton
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    // Initialize SoundManager with sound paths
    void Initialize(const std::wstring& startupSoundPath,
                    const std::wstring& syncSoundPath);

    // Play specific sounds
    bool PlayStartupSound(uint16_t delayMs = 0);
    bool PlaySyncSound(uint16_t delayMs = 0);

    // Destructor
    ~SoundManager();

   private:
    SoundManager() = default;  // Private constructor for singleton

    // Helper method to play sound
    bool PlaySoundInternal(const std::wstring& soundFilePath, uint16_t delayMs, bool playSync);

    // Mutex for thread-safe operations
    std::mutex playMutex_;

    // To keep track of asynchronous sound threads
    std::vector<std::thread> asyncThreads_;

    // Atomic flag to manage shutdown
    std::atomic<bool> shuttingDown_{false};

    // Stored sound paths
    std::wstring startupSoundPath_;
    std::wstring syncSoundPath_;
};
