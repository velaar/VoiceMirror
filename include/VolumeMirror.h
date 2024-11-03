// VolumeMirror.h
#ifndef VOLUMEMIRROR_H
#define VOLUMEMIRROR_H

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <functional>

#include "VoicemeeterManager.h"
#include "WindowsVolumeManager.h"
#include "Defconf.h" // Include Defconf.h for constants

class VolumeMirror {
public:
    VolumeMirror(int channelIdx, ChannelType type, float minDbmVal,
                float maxDbmVal, VoicemeeterManager &manager,
                bool playSound);
    ~VolumeMirror();

    void Start();
    void Stop();
    void SetPollingMode(bool enabled, int interval);

    bool GetVoicemeeterVolume(float &volumePercent, bool &isMuted);

private:
    // Immutable configuration
    const int channelIndex;
    const ChannelType channelType;
    const float minDbm;
    const float maxDbm;
    VoicemeeterManager &vmManager;
    VolumeUtils::WindowsVolumeManager windowsVolumeManager;

    // State tracking
    float lastWindowsVolume;
    bool lastWindowsMute;
    float lastVmVolume;
    bool lastVmMute;

    // Flags to ignore callbacks to prevent feedback loops
    bool ignoreWindowsChange;
    bool ignoreVoicemeeterChange;
    bool running;

    // Synchronization primitives
    std::atomic<ULONG> refCount;
    std::mutex controlMutex;
    std::mutex vmMutex;
    std::mutex soundMutex;

    // Thread for monitoring Voicemeeter
    std::thread monitorThread;

    // Sound synchronization
    const bool playSoundOnSync;
    bool pollingEnabled;
    int pollingInterval;
    std::chrono::steady_clock::time_point lastSoundPlayTime;
    const int debounceDuration; // Made const and initialized from Defconf.h

    // Flags to track origin of changes
    std::atomic<bool> changeFromWindows{false}; 

    // Callback function for Windows volume changes
    void OnWindowsVolumeChange(float newVolume, bool isMuted);

    // Monitor Voicemeeter parameters
    void MonitorVoicemeeter();

    // Update methods
    void UpdateVoicemeeterVolume(float volumePercent, bool isMuted);
    void UpdateWindowsVolume(float volumePercent, bool isMuted);

    // Play synchronization sound
    void PlaySyncSound();
};

#endif // VOLUMEMIRROR_H
