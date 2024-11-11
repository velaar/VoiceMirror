#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <optional>

#include "VoicemeeterManager.h"
#include "WindowsManager.h"
#include "VolumeUtils.h"


struct VolumeState {
    float volume;
    bool isMuted;

    bool operator!=(const VolumeState& other) const {
        return !VolumeUtils::IsFloatEqual(volume, other.volume) || isMuted != other.isMuted;
    }
};

class VolumeMirror {
   public:
    VolumeMirror(int channelIdx, ChannelType type, float minDbmVal,
                 float maxDbmVal, VoicemeeterManager& manager,
                 WindowsManager& windowsManager, bool playSound);
    ~VolumeMirror();

    void Start();
    void Stop();
    void SetPollingMode(bool enabled, int interval);

    bool GetVoicemeeterVolume(float& volumePercent, bool& isMuted);
    void UpdateVoicemeeterVolume(float volumePercent, bool isMuted);
    void UpdateWindowsVolume(float volumePercent, bool isMuted);

   private:
    void OnWindowsVolumeChange(float newVolume, bool isMuted);
    void MonitorVoicemeeter();

    int channelIndex;
    ChannelType channelType;
    float minDbm;
    float maxDbm;

    VoicemeeterManager& vmManager;
    WindowsManager& windowsManager;

    VolumeState lastWindowsState;
    VolumeState lastVmState;
    float lastVmVolume;

    std::mutex controlMutex;
    std::mutex vmMutex;

    std::thread monitorThread;
    std::atomic<bool> running;
    
    bool lastWindowsMute;
    bool ignoreWindowsChange;
    bool ignoreVoicemeeterChange;
    bool playSoundOnSync;
    float lastWindowsVolume;


    bool pollingEnabled;
    int pollingInterval;
    int debounceDuration;
    int suppressionDuration;

    std::chrono::steady_clock::time_point suppressVoicemeeterUntil;
    std::chrono::steady_clock::time_point suppressWindowsUntil;

    ChangeSource lastChangeSource;
 
    std::atomic<bool> isInitialSync;
    std::atomic<bool> isUpdatingFromWindows; // Changed to atomic

    std::optional<std::pair<float, bool>> pendingWindowsChange;
    std::chrono::steady_clock::time_point debounceTimerStart;


    // Windows volume change callback
    std::function<void(float, bool)> windowsVolumeCallback;
};
