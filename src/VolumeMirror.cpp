// VolumeMirror.cpp

#include "VolumeMirror.h"
#include "Logger.h"
#include "VoicemeeterAPI.h"
#include "VoicemeeterManager.h"
#include "COMUtilities.h"
#include <filesystem>

#include <thread>
#include <chrono>
#include <stdexcept>
#include <atomic>
#include <cmath>
#include <mutex>
#include <cstring>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <sapi.h> // For PlaySound
#include <wrl/client.h>

// Constructor
VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal, float maxDbmVal,
                           VoicemeeterManager &manager, bool playSound)
    : channelIndex(channelIdx),
      channelType(type),
      minDbm(minDbmVal),
      maxDbm(maxDbmVal),
      voicemeeterManager(manager),
      vmrAPI(nullptr),
      lastWindowsVolume(-1.0f),
      lastWindowsMute(false),
      lastVmVolume(-1.0f),
      lastVmMute(false),
      ignoreWindowsChange(false),
      ignoreVoicemeeterChange(false),
      running(false),
      refCount(1),
      playSoundOnSync(playSound),
      pollingEnabled(false),
      pollingInterval(100),
      lastSoundPlayTime(std::chrono::steady_clock::now() - std::chrono::milliseconds(pollingInterval + 10)),
      debounceDuration(50)
      //debounceDuration((((pollingInterval + 10) > (100)) ? (pollingInterval + 10) : (100))


// Default cooldown of 200 milliseconds

{
    // Get VoicemeeterAPI reference
    vmrAPI = &voicemeeterManager.GetAPI();

    // Declare HRESULT hr once
    HRESULT hr;

    // Initialize COM
    if (!InitializeCOM())
    {
        throw std::runtime_error("Failed to initialize COM.");
    }

    // Initialize event context GUID
    hr = CoCreateGuid(&eventContextGuid);
    if (FAILED(hr))
    {
        UninitializeCOM();
        throw std::runtime_error("Failed to create event context GUID.");
    }

    // Initialize Windows Audio Components
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                          __uuidof(IMMDeviceEnumerator), &deviceEnumerator);
    if (FAILED(hr))
    {
        UninitializeCOM();
        throw std::runtime_error("Failed to create MMDeviceEnumerator.");
    }

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr))
    {
        deviceEnumerator.Reset();
        UninitializeCOM();
        throw std::runtime_error("Failed to get default audio endpoint.");
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, &endpointVolume);
    if (FAILED(hr))
    {
        speakers.Reset();
        deviceEnumerator.Reset();
        UninitializeCOM();
        throw std::runtime_error("Failed to get IAudioEndpointVolume.");
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, &endpointVolume);
    if (FAILED(hr))
    {
        speakers.Reset();
        deviceEnumerator.Reset();
        UninitializeCOM();
        throw std::runtime_error("Failed to get IAudioEndpointVolume.");
    }

    // Register for volume change notifications
    hr = endpointVolume->RegisterControlChangeNotify(this);
    if (FAILED(hr))
    {
        endpointVolume.Reset();
        speakers.Reset();
        deviceEnumerator.Reset();
        UninitializeCOM();
        throw std::runtime_error("Failed to register for volume change notifications.");
    }

    // Get initial Windows volume and mute state
    float currentVolume = 0.0f;
    hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (SUCCEEDED(hr))
    {
        lastWindowsVolume = scalarToPercent(currentVolume);
    }
    else
    {
        lastWindowsVolume = 0.0f;
    }

    BOOL isMutedBOOL = FALSE;
    hr = endpointVolume->GetMute(&isMutedBOOL);
    if (SUCCEEDED(hr))
    {
        lastWindowsMute = (isMutedBOOL != FALSE);
    }
    else
    {
        lastWindowsMute = false;
    }

    // Initialize Voicemeeter channel states based on type
    InitializeVoicemeeterState();

    Logger::Instance().Log(LogLevel::DEBUG, "VolumeMirror initialized successfully.");
}

// Destructor
VolumeMirror::~VolumeMirror()
{
    Stop();

    // Unregister for volume change notifications
    if (endpointVolume)
    {
        endpointVolume->UnregisterControlChangeNotify(this);
    }

    // Join the monitoring thread if it's joinable
    if (monitorThread.joinable())
    {
        monitorThread.join();
    }

    UninitializeCOM();

    Logger::Instance().Log(LogLevel::DEBUG, "VolumeMirror destroyed.");
}

// Start the volume mirroring
void VolumeMirror::Start()
{
    std::lock_guard<std::mutex> lock(controlMutex);
    if (running)
        return;

    running = true;

    if (pollingEnabled)
    {
        // Start the monitoring thread
        monitorThread = std::thread(&VolumeMirror::MonitorVoicemeeter, this);
        Logger::Instance().Log(LogLevel::INFO, "Polling mode enabled with interval: " + std::to_string(pollingInterval) + "ms");
    }
    else
    {
        Logger::Instance().Log(LogLevel::INFO, "Polling mode disabled. Sync is one-way from Windows to Voicemeeter.");
    }

    Logger::Instance().Log(LogLevel::INFO, "VolumeMirror started.");
}

// Stop the volume mirroring
void VolumeMirror::Stop()
{
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!running)
        return;

    running = false;

    // Wait for the monitoring thread to finish
    if (monitorThread.joinable())
    {
        monitorThread.join();
    }

    Logger::Instance().Log(LogLevel::INFO, "VolumeMirror stopped.");
}

// Set polling mode and interval
void VolumeMirror::SetPollingMode(bool enabled, int interval)
{
    pollingEnabled = enabled;
    pollingInterval = interval;
    lastSoundPlayTime = std::chrono::steady_clock::now() - soundCooldownDuration;

    Logger::Instance().Log(LogLevel::INFO, "Polling mode " + std::string(enabled ? "enabled" : "disabled") +
                                               " with interval: " + std::to_string(pollingInterval) + "ms");
}

// IUnknown methods
STDMETHODIMP VolumeMirror::QueryInterface(REFIID riid, void **ppvObject)
{
    if (IID_IUnknown == riid || __uuidof(IAudioEndpointVolumeCallback) == riid)
    {
        *ppvObject = static_cast<IUnknown *>(this);
        AddRef();
        return S_OK;
    }
    else
    {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_(ULONG)
VolumeMirror::AddRef()
{
    return ++refCount;
}

STDMETHODIMP_(ULONG)
VolumeMirror::Release()
{
    ULONG ulRef = --refCount;
    if (0 == ulRef)
    {
        delete this;
    }
    return ulRef;
}

// OnNotify implementation - called when Windows volume changes
STDMETHODIMP VolumeMirror::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
{
    // Check if the volume change originated from our own process
    if (pNotify->guidEventContext == eventContextGuid)
    {
        Logger::Instance().Log(LogLevel::DEBUG, "Ignored volume change notification from our own process.");
        return S_OK;
    }

    if (ignoreWindowsChange)
        return S_OK;

    std::lock_guard<std::mutex> lock(vmMutex);

    float newVolume = scalarToPercent(pNotify->fMasterVolume);
    bool newMute = (pNotify->bMuted != FALSE);

    // If Windows volume or mute state has changed
    if (!IsFloatEqual(newVolume, lastWindowsVolume) || newMute != lastWindowsMute)
    {
        lastWindowsVolume = newVolume;
        lastWindowsMute = newMute;

        // Update Voicemeeter volume accordingly
        ignoreVoicemeeterChange = true;
        UpdateVoicemeeterVolume(newVolume, newMute);
        ignoreVoicemeeterChange = false;
    }

    return S_OK;
}

// Initialize Voicemeeter channel states based on type
void VolumeMirror::InitializeVoicemeeterState()
{
    float gain = 0.0f;
    float muteVal = 0.0f;
    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input)
    {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    }
    else // ChannelType::Output
    {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (vmrAPI->GetParameterFloat(gainParam.c_str(), &gain) == 0)
    {
        lastVmVolume = dBmToPercent(gain);
    }
    else
    {
        lastVmVolume = 0.0f;
    }

    if (vmrAPI->GetParameterFloat(muteParam.c_str(), &muteVal) == 0)
    {
        lastVmMute = (muteVal != 0.0f);
    }
    else
    {
        lastVmMute = false;
    }

    Logger::Instance().Log(LogLevel::DEBUG, "Initial Voicemeeter Volume: " +
                                                std::to_string(lastVmVolume.load()) + "% " +
                                                (lastVmMute.load() ? "(Muted)" : "(Unmuted)"));
}

// Monitor Voicemeeter parameters in a separate thread
void VolumeMirror::MonitorVoicemeeter()
{
    if (!InitializeCOM())
    {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize COM in MonitorVoicemeeter thread.");
        return;
    }

    std::chrono::steady_clock::time_point lastVmChangeTime = std::chrono::steady_clock::now() - debounceDuration;
    bool pendingSound = false;

    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));

        if (ignoreVoicemeeterChange)
            continue;

        if (vmrAPI->IsParametersDirty() > 0)
        {
            std::lock_guard<std::mutex> lock(vmMutex);

            float vmVolume = 0.0f;
            bool vmMute = false;
            if (GetVoicemeeterVolume(vmVolume, vmMute))
            {
                if (!IsFloatEqual(vmVolume, lastVmVolume) || vmMute != lastVmMute)
                {
                    lastVmVolume = vmVolume;
                    lastVmMute = vmMute;

                    // Update Windows volume accordingly
                    ignoreWindowsChange = true;
                    UpdateWindowsVolume(vmVolume, vmMute);
                    ignoreWindowsChange = false;

                    // Mark that a change has occurred and reset the timer
                    lastVmChangeTime = std::chrono::steady_clock::now();
                    pendingSound = true;

                    Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter volume change detected. Scheduling synchronization sound.");
                }
            }
        }

        // Check if the debounce period has passed since the last change
        if (pendingSound)
        {
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastChange = now - lastVmChangeTime;
            if (timeSinceLastChange >= debounceDuration)
            {
                if (playSoundOnSync)
                {
                    Logger::Instance().Log(LogLevel::DEBUG, "Debounce period elapsed. Playing synchronization sound.");
                    PlaySyncSound();
                }
                pendingSound = false;
            }
        }
    }
    UninitializeCOM();
}

// Get the current Voicemeeter volume and mute state
bool VolumeMirror::GetVoicemeeterVolume(float &volumePercent, bool &isMuted)
{
    float gainValue = 0.0f;
    float muteValue = 0.0f;
    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input)
    {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    }
    else // ChannelType::Output
    {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (vmrAPI->GetParameterFloat(gainParam.c_str(), &gainValue) != 0)
    {
        return false;
    }

    if (vmrAPI->GetParameterFloat(muteParam.c_str(), &muteValue) != 0)
    {
        return false;
    }

    volumePercent = dBmToPercent(gainValue);
    isMuted = (muteValue != 0.0f);

    return true;
}

// Update Voicemeeter volume and mute state based on Windows volume
void VolumeMirror::UpdateVoicemeeterVolume(float volumePercent, bool isMuted)
{
    float dBmValue = percentToDbm(volumePercent);

    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input)
    {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    }
    else // ChannelType::Output
    {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    vmrAPI->SetParameterFloat(gainParam.c_str(), dBmValue);
    vmrAPI->SetParameterFloat(muteParam.c_str(), isMuted ? 1.0f : 0.0f);

    Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter volume updated: " + std::to_string(volumePercent) + "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
}

// Update Windows volume and mute state based on Voicemeeter volume
void VolumeMirror::UpdateWindowsVolume(float volumePercent, bool isMuted)
{
    float scalarValue = percentToScalar(volumePercent);

    HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(scalarValue, &eventContextGuid);
    if (FAILED(hr))
    {
        Logger::Instance().Log(LogLevel::ERR, "Failed to set Windows master volume.");
    }

    hr = endpointVolume->SetMute(isMuted, &eventContextGuid);
    if (FAILED(hr))
    {
        Logger::Instance().Log(LogLevel::ERR, "Failed to set Windows mute state.");
    }

    Logger::Instance().Log(LogLevel::DEBUG, "Windows volume updated: " + std::to_string(volumePercent) + "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
}

// Convert a scalar volume value (0.0 - 1.0) to a percentage (0 - 100)
float VolumeMirror::scalarToPercent(float scalar)
{
    return scalar * 100.0f;
}

// Convert a percentage volume value (0 - 100) to a scalar (0.0 - 1.0)
float VolumeMirror::percentToScalar(float percent)
{
    return percent / 100.0f;
}

// Convert a dBm gain value to a percentage volume (0 - 100)
float VolumeMirror::dBmToPercent(float dBm)
{
    if (dBm <= minDbm)
        return 0.0f;
    if (dBm >= maxDbm)
        return 100.0f;

    return ((dBm - minDbm) / (maxDbm - minDbm)) * 100.0f;
}

// Convert a percentage volume (0 - 100) to a dBm gain value
float VolumeMirror::percentToDbm(float percent)
{
    if (percent <= 0.0f)
        return minDbm;
    if (percent >= 100.0f)
        return maxDbm;

    return ((percent / 100.0f) * (maxDbm - minDbm)) + minDbm;
}

void VolumeMirror::PlaySyncSound()
{
    std::lock_guard<std::mutex> lock(soundMutex);
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastSound = now - lastSoundPlayTime;

    const std::wstring soundFilePath = L"C:\\Windows\\Media\\Windows Unlock.wav";
    BOOL played = FALSE;

    // Attempt to play the specified sound file
    played = PlaySoundW(soundFilePath.c_str(), NULL, SND_FILENAME | SND_ASYNC);

    if (!played)
    {
        // If the sound file does not exist or fails to play, play the default chime
        PlaySound(TEXT("Asterisk"), NULL, SND_ALIAS | SND_ASYNC);
        Logger::Instance().Log(LogLevel::DEBUG, "Played default system sound.");
    }
    else
    {
        Logger::Instance().Log(LogLevel::DEBUG, "Played custom sync sound.");
    }

    lastSoundPlayTime = now;
}


// Helper method to check float equality with a tolerance
bool VolumeMirror::IsFloatEqual(float a, float b, float epsilon)
{
    return std::fabs(a - b) < epsilon;
}
