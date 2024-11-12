// SoundManager.cpp
#include "SoundManager.h"
#include <Windows.h>
#include <future> // For std::async

// Helper function to convert wstring to string for logging (done in place)
std::string WideStringToString(const std::wstring& wstr) {
    int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(bufferSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], bufferSize, nullptr, nullptr);
    return str;
}

// Singleton instance access
SoundManager& SoundManager::Instance() {
    static SoundManager instance;
    return instance;
}

// Initialize SoundManager with sound paths
void SoundManager::Initialize(const std::wstring& startupSoundPath, const std::wstring& syncSoundPath) {
    startupSoundPath_ = startupSoundPath;
    syncSoundPath_ = syncSoundPath;
    LOG_INFO("[SoundManager::Initialize] SoundManager initialized with provided sound paths.");
}

// Destructor
SoundManager::~SoundManager() {
    shuttingDown_ = true;
    LOG_INFO("[SoundManager::~SoundManager] SoundManager shut down gracefully.");
}

// Play Startup Sound
bool SoundManager::PlayStartupSound(uint16_t delayMs) {
    if (startupSoundPath_.empty()) {
        LOG_WARNING("[SoundManager::PlayStartupSound] Startup sound path is empty. Skipping playback.");
        return false;
    }
    return PlaySoundInternal(startupSoundPath_, delayMs, true);
}

// Play Sync Sound
bool SoundManager::PlaySyncSound(uint16_t delayMs) {
    if (syncSoundPath_.empty()) {
        LOG_WARNING("[SoundManager::PlaySyncSound] Sync sound path is empty. Skipping playback.");
        return false;
    }
    return PlaySoundInternal(syncSoundPath_, delayMs, true);
}

// Play Sound Internally
bool SoundManager::PlaySoundInternal(const std::wstring& soundFilePath, uint16_t delayMs, bool playSync) {
    if (shuttingDown_) {
        LOG_WARNING("[SoundManager::PlaySoundInternal] Shutdown in progress. Aborting sound playback.");
        return false;
    }

    // Lambda to handle playback
    auto playSound = [this, soundFilePath, delayMs, playSync]() {
        if (delayMs > 0) {
            LOG_DEBUG("[SoundManager::PlaySoundInternal] Delaying sound playback by " + std::to_string(delayMs) + " ms.");
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        // Validate sound file path
        if (soundFilePath.empty()) {
            LOG_ERROR("[SoundManager::PlaySoundInternal] Sound file path is empty.");
            return;
        }

        // Check if file exists
        DWORD fileAttrib = GetFileAttributesW(soundFilePath.c_str());
        if (fileAttrib == INVALID_FILE_ATTRIBUTES || (fileAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            LOG_ERROR("[SoundManager::PlaySoundInternal] Sound file does not exist or is a directory: " + WideStringToString(soundFilePath));
            return;
        }

        // Play the sound and add SND_PURGE after
        LOG_INFO("[SoundManager::PlaySoundInternal] Playing sound: " + WideStringToString(soundFilePath) + (playSync ? " synchronously." : " asynchronously."));
        BOOL result = PlaySoundW(soundFilePath.c_str(), NULL, SND_FILENAME | (playSync ? SND_SYNC : SND_ASYNC));
        if (!result) {
            LOG_ERROR("[SoundManager::PlaySoundInternal] Failed to play sound. Error code: " + std::to_string(GetLastError()));
        } else {
            LOG_INFO("[SoundManager::PlaySoundInternal] Sound played successfully.");
        }

        // Immediately release memory associated with sound
        PlaySoundW(NULL, NULL, SND_PURGE);
    };

    if (playSync) {
        // Synchronous playback
        playSound();
    } else {
        // Asynchronous playback using std::async for automatic thread management
        std::future<void> asyncResult = std::async(std::launch::async, playSound);
        LOG_INFO("[SoundManager::PlaySoundInternal] Asynchronous sound playback started.");
    }

    return true;
}
