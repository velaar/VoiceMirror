#pragma once

#include "VoicemeeterAPI.h"
#include "VolumeUtils.h"
#include <string>
#include <mutex>
#include <unordered_map>

// Struct for holding volume and mute state
struct ChannelState {
    float volume;
    bool isMuted;
};

class VoicemeeterManager {
public:
    VoicemeeterManager();
    ~VoicemeeterManager();

    bool Initialize(int voicemeeterType);
    void Shutdown();
    void ShutdownCommand();
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);
    void SetDebugMode(bool newDebugMode);
    bool GetDebugMode();
    VoicemeeterAPI& GetAPI();

    // Channel management and information retrieval
    void ListAllChannels();
    void ListInputs();
    void ListOutputs();
    bool GetVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float &volumePercent, bool &isMuted);
    void UpdateVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float volumePercent, bool isMuted);
    bool IsParametersDirty();
    void ListMonitorableDevices();

    // Channel state management
    bool InitializeVoicemeeterState(int channelIndex, VolumeUtils::ChannelType channelType);
    float GetChannelVolume(int channelIndex, VolumeUtils::ChannelType channelType);
    bool IsChannelMuted(int channelIndex, VolumeUtils::ChannelType channelType);
    
    // COM initialization methods
    bool InitializeCOM();
    void UninitializeCOM();

private:
    VoicemeeterAPI vmrAPI;
    bool loggedIn;
    bool debugMode;
    bool comInitialized;
    std::mutex toggleMutex;



    // Channel states
    std::unordered_map<int, ChannelState> channelStates;
    std::mutex channelStatesMutex;
};
