// VolumeMirror.h
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "Defconf.h"
#include "VoicemeeterManager.h"
#include "WindowsManager.h"

class VolumeMirror {
   public:
    enum class Mode { Polling,
                      Callback,
                      Hybrid };

    static VolumeMirror& Instance(int channelIdx, ChannelType type, VoicemeeterManager& manager, WindowsManager& windowsManager, Mode mode) {
        static VolumeMirror instance(channelIdx, type, manager, windowsManager, mode);
        return instance;
    }
    ~VolumeMirror();

    VolumeMirror(const VolumeMirror&) = delete;
    VolumeMirror& operator=(const VolumeMirror&) = delete;

    void Start();
    void Stop();

   private:
    VolumeMirror(int channelIdx, ChannelType type, VoicemeeterManager& manager, WindowsManager& windowsManager, Mode mode);
    void OnWindowsVolumeChange(float newVolume, bool isMuted);
    void MonitorVolumes();

    int channelIndex;
    ChannelType channelType;

    VoicemeeterManager& vmManager;
    WindowsManager& windowsManager;

    Mode mode;

    std::atomic<bool> running;
    int pollingInterval;

    std::thread monitorThread;

    std::mutex controlMutex;

    std::function<void(float, bool)> windowsVolumeCallback;
    unsigned int windowsVolumeCallbackID;

    float lastVmVolume;
    bool lastVmMute;
    float lastWinVolume;
    bool lastWinMute;

    bool updatingVoicemeeter;
    bool updatingWindows;

    float pendingVmVolume;
    bool pendingVmMute;
    bool vmChangePending;
};
