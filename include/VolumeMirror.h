// VolumeMirror.h

#pragma once

#include <atomic>
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

    ChangeSource lastChangeSource;

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

    std::atomic<bool> ignoreWindowsChange;
    std::atomic<bool> ignoreVoicemeeterChange;
    bool running;
    bool playSoundOnSync;
    bool pollingEnabled;
    int pollingInterval;
    int debounceDuration;

    std::mutex vmMutex;
    std::mutex controlMutex;
    std::thread monitorThread;
    int refCount;
    std::chrono::steady_clock::time_point lastWindowsChangeTime;
    std::function<void(float, bool)> windowsVolumeCallback;
};
