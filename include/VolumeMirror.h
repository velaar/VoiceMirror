#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

#include "VoicemeeterManager.h"
#include "WindowsManager.h"


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

    // Suppression duration in milliseconds
    static const int SUPPRESSION_DURATION_MS = 200; // Adjust as needed

    int channelIndex;
    ChannelType channelType;
    float minDbm;
    float maxDbm;

    VoicemeeterManager& vmManager;
    WindowsManager& windowsManager;

    float lastWindowsVolume;
    bool lastWindowsMute;
    float lastVmVolume;
    bool lastVmMute;

    bool ignoreWindowsChange;
    bool ignoreVoicemeeterChange;
    bool running;
    bool playSoundOnSync;
    bool pollingEnabled;
    int pollingInterval;
    int debounceDuration;

    std::mutex vmMutex;       // Protects Voicemeeter-related variables
    std::mutex controlMutex;  // Protects control variables
    std::thread monitorThread;
    int refCount;

    // Change source tracking
    ChangeSource lastChangeSource;

    // Suppression windows
    std::chrono::steady_clock::time_point suppressVoicemeeterUntil;
    std::chrono::steady_clock::time_point suppressWindowsUntil;

    // Windows volume change callback
    std::function<void(float, bool)> windowsVolumeCallback;
};
