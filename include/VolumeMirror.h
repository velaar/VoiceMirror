#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>


#include "Defconf.h" 
#include "VoicemeeterManager.h"
#include "VolumeUtils.h"
#include "WindowsManager.h"

struct VolumeState {
    float volume;
    bool isMuted;

    bool operator!=(const VolumeState& other) const {
        return !VolumeUtils::IsFloatEqual(volume, other.volume) || isMuted != other.isMuted;
    }
};

class VolumeMirror {
   public:
    VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal,
                               float maxDbmVal, VoicemeeterManager& manager,
                               WindowsManager& windowsManager,
                               bool playSound);
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
    UpdateSource lastUpdateSource;

    std::atomic<bool> isInitialSync;
    CallbackID windowsVolumeCallbackID;
    std::function<void(float, bool)> windowsVolumeCallback;
};
