// VolumeMirror.h

#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <windows.h>
#include <mmsystem.h>
#include "VoicemeeterManager.h" // Use VoicemeeterManager instead of VoicemeeterAPI

enum class ChannelType {
    Input,
    Output
};

class VolumeMirror : public IAudioEndpointVolumeCallback
{
public:
    VolumeMirror(int channelIdx, ChannelType type, float minDbmVal, float maxDbmVal, VoicemeeterManager& manager, long voicemeeterType, bool playSoundOnSync);
    ~VolumeMirror();

    void Start();
    void Stop();

    // IUnknown methods
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvInterface) override;

    // IAudioEndpointVolumeCallback method
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

private:
    // Prevent copying
    VolumeMirror(const VolumeMirror&) = delete;
    VolumeMirror& operator=(const VolumeMirror&) = delete;
    VoicemeeterAPI* vmrAPI; // Add a pointer to VoicemeeterAPI

    bool playSoundOnSync; // Add a flag to control sound playback
    float dBmToPercent(float dbm);
    float percentToScalar(float percent);
    float scalarToPercent(float scalar);
    float percentToDbm(float percent);

    void MonitorVoicemeeter();
    void Log(const std::string& message);

    // Member variables
    int channelIndex;
    ChannelType channelType;
    float minDbm;
    float maxDbm;
    bool debug;

    // COM interfaces
    IMMDeviceEnumerator* deviceEnumerator;
    IMMDevice* speakers;
    IAudioEndpointVolume* endpointVolume;

    // Voicemeeter Manager reference
    VoicemeeterManager& voicemeeterManager;

    // Last known states
    std::atomic<float> lastWindowsVolume;
    std::atomic<bool> lastWindowsMute;

    std::atomic<float> lastVmVolume;
    std::atomic<bool> lastVmMute;

    // Flags to prevent recursive updates
    std::atomic<bool> ignoreWindowsChange;
    std::atomic<bool> ignoreVoicemeeterChange;

    // Threading
    std::thread vmThread;
    std::atomic<bool> running;
    std::mutex stateMutex;

    // Reference counting for IAudioEndpointVolumeCallback
    std::atomic<ULONG> refCount;

    std::chrono::steady_clock::time_point lastChangeTime;
};
