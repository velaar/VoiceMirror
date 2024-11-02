#ifndef VOICEMEETERMANAGER_H
#define VOICEMEETERMANAGER_H

#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include "VoicemeeterAPI.h"

class VoicemeeterManager {
   public:
    VoicemeeterManager();
    bool Initialize(int voicemeeterType);
    void Shutdown();
    void ShutdownCommand();
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);
    void SetDebugMode(bool newDebugMode);
    bool GetDebugMode();
    VoicemeeterAPI& GetAPI();
    void ListAllChannels();
    void ListInputs();
    void ListOutputs();

    bool SetChannelParameter(const std::string& type, int index, const std::string& parameter, float value);
    float GetChannelParameter(const std::string& type, int index, const std::string& parameter);
    bool ToggleChannelMute(const std::string& type, int index);
    void SyncChannelVolumes(const std::string& type, int index1, int index2);
    bool IsChannelMuted(const std::string& type, int index);

   private:
    VoicemeeterAPI vmrAPI;
    bool loggedIn;
    bool debugMode;
    std::mutex toggleMutex;
    
    struct ChannelState {
        float volume;
        bool muted;
    };
    std::unordered_map<std::string, ChannelState> channelStates;
    
    bool ValidateChannelType(const std::string& type) const;
    bool ValidateChannelIndex(const std::string& type, int index) const;
    std::string GetChannelIdentifier(const std::string& type, int index) const;
};

#endif  // VOICEMEETERMANAGER_H
