// src/VolumeMirror.cpp

#include "VolumeMirror.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

// Constructor
VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal, float maxDbmVal, bool debugMode)
    : channelIndex(channelIdx), channelType(type),
      minDbm(minDbmVal), maxDbm(maxDbmVal),
      debug(debugMode),
      lastWindowsVolume(-1.0f), lastWindowsMute(false),
      lastVmVolume(-1.0f), lastVmMute(false),
      ignoreWindowsChange(false), ignoreVoicemeeterChange(false),
      running(false)
{
    // Initialize COM
    CoInitialize(NULL);

    // Initialize Windows Audio Components
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize MMDeviceEnumerator.");
    }

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get default audio endpoint.");
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpointVolume);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get AudioEndpointVolume.");
    }

    // Initialize Voicemeeter API
    if (!vmrAPI.Initialize()) {
        throw std::runtime_error("Failed to initialize Voicemeeter API.");
    }

    if (vmrAPI.Login() != 0 && vmrAPI.Login() != 1)
    {
        throw std::runtime_error("Failed to connect to Voicemeeter.");
    }

    // Initialize last known states
    float currentVolume = 0.0f;
    endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    lastWindowsVolume = currentVolume * 100.0f;

    BOOL isMuted = FALSE;
    endpointVolume->GetMute(&isMuted);
    lastWindowsMute = isMuted;

    // Initialize Voicemeeter channel states based on type
    if (channelType == ChannelType::Output)
    {
        // For Bus
        float gain = 0.0f;
        if (vmrAPI.GetParameterFloat("Bus[0].Gain", &gain) == 0) {
            lastVmVolume = dBmToPercent(gain);
        }
        else {
            lastVmVolume = 0.0f;
        }

        float muteVal = 0.0f;
        if (vmrAPI.GetParameterFloat("Bus[0].Mute", &muteVal) == 0) {
            lastVmMute = (muteVal != 0.0f);
        }
        else {
            lastVmMute = false;
        }

        Log("Initial Voicemeeter Output Bus " + std::to_string(channelIndex) + " Volume: " +
            std::to_string(lastVmVolume.load()) + "% " +
            (lastVmMute.load() ? "(Muted)" : "(Unmuted)"));
    }
    else if (channelType == ChannelType::Input)
    {
        // For Strip
        float gain = 0.0f;
        if (vmrAPI.GetParameterFloat("Strip[0].Gain", &gain) == 0) {
            lastVmVolume = dBmToPercent(gain);
        }
        else {
            lastVmVolume = 0.0f;
        }

        float muteVal = 0.0f;
        if (vmrAPI.GetParameterFloat("Strip[0].Mute", &muteVal) == 0) {
            lastVmMute = (muteVal != 0.0f);
        }
        else {
            lastVmMute = false;
        }

        Log("Initial Voicemeeter Input Strip " + std::to_string(channelIndex) + " Volume: " +
            std::to_string(lastVmVolume.load()) + "% " +
            (lastVmMute.load() ? "(Muted)" : "(Unmuted)"));
    }

    Log("Initial Windows Volume: " + std::to_string(lastWindowsVolume.load()) + "% " +
        (lastWindowsMute.load() ? "(Muted)" : "(Unmuted)"));
}

// Destructor
VolumeMirror::~VolumeMirror()
{
    running = false;
    if (vmThread.joinable())
        vmThread.join();

    if (endpointVolume)
        endpointVolume->Release();
    if (speakers)
        speakers->Release();
    if (deviceEnumerator)
        deviceEnumerator->Release();

    vmrAPI.Logout();
    vmrAPI.Shutdown();
    CoUninitialize();
}

void VolumeMirror::Start()
{
    running = true;

    // Start Voicemeeter monitoring thread
    vmThread = std::thread(&VolumeMirror::MonitorVoicemeeter, this);

    // Start polling Windows volume in a separate thread
    std::thread windowsThread(&VolumeMirror::PollWindowsVolume, this);
    windowsThread.detach();

    // Register callback for Windows volume changes
    // Implemented via COM event subscription if needed
}

void VolumeMirror::PollWindowsVolume()
{
    while (running)
    {
        float currentVolume = 0.0f;
        endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
        currentVolume *= 100.0f;

        BOOL isMuted = FALSE;
        endpointVolume->GetMute(&isMuted);

        if (fabs(currentVolume - lastWindowsVolume) > 1.0f || isMuted != lastWindowsMute)
        {
            if (!ignoreVoicemeeterChange)
            {
                float mappedVolume = percentToDbm(currentVolume);
                ignoreWindowsChange = true;

                if (channelType == ChannelType::Output)
                {
                    vmrAPI.SetParameterFloat("Bus[0].Gain", mappedVolume);
                    vmrAPI.SetParameterFloat("Bus[0].Mute", isMuted ? 1.0f : 0.0f);
                    Log("Windows Volume: " + std::to_string(currentVolume) + "% " +
                        (isMuted ? "(Muted)" : "(Unmuted)") +
                        " -> Voicemeeter Output Bus " + std::to_string(channelIndex) +
                        " Volume: " + std::to_string(currentVolume) + "% " +
                        (isMuted ? "(Muted)" : "(Unmuted)"));
                }
                else if (channelType == ChannelType::Input)
                {
                    vmrAPI.SetParameterFloat("Strip[0].Gain", mappedVolume);
                    vmrAPI.SetParameterFloat("Strip[0].Mute", isMuted ? 1.0f : 0.0f);
                    Log("Windows Volume: " + std::to_string(currentVolume) + "% " +
                        (isMuted ? "(Muted)" : "(Unmuted)") +
                        " -> Voicemeeter Input Strip " + std::to_string(channelIndex) +
                        " Volume: " + std::to_string(currentVolume) + "% " +
                        (isMuted ? "(Muted)" : "(Unmuted)"));
                }

                lastWindowsVolume = currentVolume;
                lastWindowsMute = isMuted;
            }
            else
            {
                ignoreVoicemeeterChange = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void VolumeMirror::MonitorVoicemeeter()
{
    while (running)
    {
        try
        {
            // Check if parameters have changed
            if (vmrAPI.IsParametersDirty() > 0)
            {
                float vmGain = 0.0f;
                bool vmMute = false;

                if (channelType == ChannelType::Output)
                {
                    if (vmrAPI.GetParameterFloat("Bus[0].Gain", &vmGain) != 0)
                        vmGain = 0.0f;

                    float muteVal = 0.0f;
                    if (vmrAPI.GetParameterFloat("Bus[0].Mute", &muteVal) == 0) {
                        vmMute = (muteVal != 0.0f);
                    }
                }
                else if (channelType == ChannelType::Input)
                {
                    if (vmrAPI.GetParameterFloat("Strip[0].Gain", &vmGain) != 0)
                        vmGain = 0.0f;

                    float muteVal = 0.0f;
                    if (vmrAPI.GetParameterFloat("Strip[0].Mute", &muteVal) == 0) {
                        vmMute = (muteVal != 0.0f);
                    }
                }

                float mappedVmVolume = dBmToPercent(vmGain);

                if (ignoreWindowsChange)
                {
                    ignoreWindowsChange = false;
                }
                else if (fabs(mappedVmVolume - lastVmVolume) > 1.0f || vmMute != lastVmMute)
                {
                    // Update Windows volume
                    ignoreVoicemeeterChange = true;
                    float volumeScalar = percentToDbm(mappedVmVolume) / 100.0f;
                    endpointVolume->SetMasterVolumeLevelScalar(volumeScalar, NULL);
                    endpointVolume->SetMute(vmMute, NULL);

                    if (channelType == ChannelType::Output)
                    {
                        Log("Voicemeeter Output Bus " + std::to_string(channelIndex) +
                            " Volume: " + std::to_string(mappedVmVolume) + "% " +
                            (vmMute ? "(Muted)" : "(Unmuted)") +
                            " -> Windows Volume: " + std::to_string(mappedVmVolume) + "% " +
                            (vmMute ? "(Muted)" : "(Unmuted)"));
                    }
                    else if (channelType == ChannelType::Input)
                    {
                        Log("Voicemeeter Input Strip " + std::to_string(channelIndex) +
                            " Volume: " + std::to_string(mappedVmVolume) + "% " +
                            (vmMute ? "(Muted)" : "(Unmuted)") +
                            " -> Windows Volume: " + std::to_string(mappedVmVolume) + "% " +
                            (vmMute ? "(Muted)" : "(Unmuted)"));
                    }

                    lastVmVolume = mappedVmVolume;
                    lastVmMute = vmMute;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        catch (const std::exception& ex)
        {
            Log(std::string("Error in Voicemeeter monitoring: ") + ex.what());
        }
    }
}

float VolumeMirror::dBmToPercent(float dbm)
{
    if (dbm < minDbm)
        dbm = minDbm;
    if (dbm > maxDbm)
        dbm = maxDbm;
    return ((dbm - minDbm) / (maxDbm - minDbm)) * 100.0f;
}

float VolumeMirror::percentToDbm(float percent)
{
    return (percent / 100.0f) * (maxDbm - minDbm) + minDbm;
}

void VolumeMirror::Log(const std::string& message)
{
    if (debug)
    {
        std::cout << "[DEBUG] " << message << std::endl;
    }
}
