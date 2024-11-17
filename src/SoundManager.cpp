// SoundManager.cpp
#include "SoundManager.h"
#include <Windows.h>
#include <future> // For std::async
#include "VolumeUtils.h"
#include <atomic> // For thread-safe flags

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
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    return PlaySoundInternal(startupSoundPath_, delayMs, true); // Play synchronously
}

// Play Sync Sound
bool SoundManager::PlaySyncSound(uint16_t delayMs) {
    if (syncSoundPath_.empty()) {
        LOG_WARNING("[SoundManager::PlaySyncSound] Sync sound path is empty. Skipping playback.");
        return false;
    }
    return PlaySoundInternal(syncSoundPath_, delayMs, false); // Play asynchronously
}

// Play Sound Internally
bool SoundManager::PlaySoundInternal(const std::wstring& soundFilePath, uint16_t delayMs, bool playSync) {
    if (shuttingDown_) {
        LOG_WARNING(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Shutdown in progress. Aborting sound playback."));
        return false;
    }

    auto playSound = [this, soundFilePath, delayMs, playSync]() {
        if (delayMs > 0) {
            LOG_DEBUG(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Delaying sound playback by " + std::to_wstring(delayMs) + L" ms."));
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        if (soundFilePath.empty()) {
            LOG_ERROR(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Sound file path is empty."));
            return;
        }

        DWORD fileAttrib = GetFileAttributesW(soundFilePath.c_str());
        if (fileAttrib == INVALID_FILE_ATTRIBUTES || (fileAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            LOG_ERROR(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Sound file does not exist or is a directory: " + soundFilePath));
            return;
        }

        LOG_INFO(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Playing sound: " + soundFilePath + (playSync ? L" synchronously." : L" asynchronously.")));
        BOOL result = PlaySoundW(soundFilePath.c_str(), NULL, SND_FILENAME | (playSync ? SND_SYNC : SND_ASYNC));
        if (!result) {
            LOG_ERROR(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Failed to play sound. Error code: " + std::to_wstring(GetLastError())));
        } else {
            LOG_INFO(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Sound played successfully."));
        }

        // Only purge if the sound was played synchronously
        if (playSync) {
            PlaySoundW(NULL, NULL, SND_PURGE);
            LOG_DEBUG("[SoundManager::PlaySoundInternal] Purged sound playback.");
        }
    };

    if (playSync) {
        playSound(); // Synchronous playback
    } else {
        // Asynchronous playback
        std::future<void> asyncResult = std::async(std::launch::async, playSound);
        LOG_INFO(VolumeUtils::ConvertWStringToString(L"[SoundManager::PlaySoundInternal] Asynchronous sound playback started."));
    }

    return true;
}
