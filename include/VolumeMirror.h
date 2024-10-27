// include/VolumeMirror.h

#ifndef VOLUMEMIRROR_H
#define VOLUMEMIRROR_H

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <thread>
#include <atomic>
#include <string>
#include "VoicemeeterAPI.h"

// Define the type of channel
enum class ChannelType
{
    Input,
    Output
};

// VolumeMirror Class
class VolumeMirror
{
public:
    VolumeMirror(int channelIndex, ChannelType channelType, float minDbm, float maxDbm, bool debugMode = false);
    ~VolumeMirror();

    void Start();

private:
    // Windows Audio Components
    IMMDeviceEnumerator* deviceEnumerator = nullptr;
    IMMDevice* speakers = nullptr;
    IAudioEndpointVolume* endpointVolume = nullptr;

    // Voicemeeter API
    VoicemeeterAPI vmrAPI;

    // Configuration
    int channelIndex;
    float minDbm;
    float maxDbm;
    ChannelType channelType;
    bool debug;

    // Volume States
    std::atomic<float> lastWindowsVolume;
    std::atomic<bool> lastWindowsMute;
    std::atomic<float> lastVmVolume;
    std::atomic<bool> lastVmMute;

    // Flags to prevent feedback loops
    std::atomic<bool> ignoreWindowsChange;
    std::atomic<bool> ignoreVoicemeeterChange;

    // Threading
    std::thread vmThread;
    std::atomic<bool> running;

    // Private Methods
    void MonitorVoicemeeter();
    void PollWindowsVolume();
    float dBmToPercent(float dbm);
    float percentToDbm(float percent);
    void Log(const std::string& message);
};

#endif // VOLUMEMIRROR_H
