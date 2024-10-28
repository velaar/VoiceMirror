// src/VolumeMirror.cpp

#include "VolumeMirror.h"

// Constructor
VolumeMirror::VolumeMirror(int channelIdx, ChannelType type, float minDbmVal, float maxDbmVal, VoicemeeterManager& manager, long voicemeeterType, bool playSound)
     : channelIndex(channelIdx), channelType(type),
      minDbm(minDbmVal), maxDbm(maxDbmVal),
      debug(manager.GetDebugMode()),
      voicemeeterManager(manager), 
      lastWindowsVolume(-1.0f), lastWindowsMute(false),
      lastVmVolume(-1.0f), lastVmMute(false),
      ignoreWindowsChange(false), ignoreVoicemeeterChange(false),
      running(false),
      refCount(1),
      vmrAPI(nullptr),
      playSoundOnSync(playSound) 
{
    // Initialize VoicemeeterManager with voicemeeterType if not already initialized
    if (!voicemeeterManager.Initialize(voicemeeterType)) {
        endpointVolume->UnregisterControlChangeNotify(this);
        endpointVolume->Release();
        speakers->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        throw std::runtime_error("Failed to initialize Voicemeeter API.");
    } 

    vmrAPI = &voicemeeterManager.GetAPI(); 
    
    // Initialize COM
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize COM.");
    }

    // Initialize Windows Audio Components
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr))
    {
        CoUninitialize();
        throw std::runtime_error("Failed to create MMDeviceEnumerator.");
    }

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &speakers);
    if (FAILED(hr))
    {
        deviceEnumerator->Release();
        CoUninitialize();
        throw std::runtime_error("Failed to get default audio endpoint.");
    }

    hr = speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpointVolume);
    if (FAILED(hr))
    {
        speakers->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        throw std::runtime_error("Failed to get IAudioEndpointVolume.");
    }

    // Register for volume change notifications
    hr = endpointVolume->RegisterControlChangeNotify(this);
    if (FAILED(hr))
    {
        endpointVolume->Release();
        speakers->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        throw std::runtime_error("Failed to register for volume change notifications.");
    }

    float currentVolume = 0.0f;
    endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    lastWindowsVolume = scalarToPercent(currentVolume);

    BOOL isMutedBOOL = FALSE;
    endpointVolume->GetMute(&isMutedBOOL);
    lastWindowsMute = (isMutedBOOL != FALSE);

    // Initialize Voicemeeter channel states based on type
    if (channelType == ChannelType::Output)
    {
        // For Bus
        float gain = 0.0f;
        if (vmrAPI->GetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Gain").c_str(), &gain) == 0) {
            lastVmVolume = dBmToPercent(gain);
        }
        else {
            lastVmVolume = 0.0f;
        }

        float muteVal = 0.0f;
        if (vmrAPI->GetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Mute").c_str(), &muteVal) == 0) {
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
        if (vmrAPI->GetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Gain").c_str(), &gain) == 0) {
            lastVmVolume = dBmToPercent(gain);
        }
        else {
            lastVmVolume = 0.0f;
        }

        float muteVal = 0.0f;
        if (vmrAPI->GetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Mute").c_str(), &muteVal) == 0) {
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
    Stop();

    // Unregister for volume change notifications
    if (endpointVolume)
    {
        endpointVolume->UnregisterControlChangeNotify(this);
        endpointVolume->Release();
    }
    if (speakers)
        speakers->Release();
    if (deviceEnumerator)
        deviceEnumerator->Release();

    voicemeeterManager.Shutdown();
    CoUninitialize();
}

// Start the synchronization
void VolumeMirror::Start()
{
    running = true;

    // Start Voicemeeter monitoring thread
    vmThread = std::thread(&VolumeMirror::MonitorVoicemeeter, this);
}

// Stop the synchronization
void VolumeMirror::Stop()
{
    running = false;
    if (vmThread.joinable())
        vmThread.join();
}

// IUnknown methods
STDMETHODIMP_(ULONG) VolumeMirror::AddRef()
{
    return refCount.fetch_add(1) + 1;
}

STDMETHODIMP_(ULONG) VolumeMirror::Release()
{
    ULONG count = refCount.fetch_sub(1) - 1;
    if (count == 0)
    {
        delete this;
    }
    return count;
}

STDMETHODIMP VolumeMirror::QueryInterface(REFIID riid, void **ppvInterface)
{
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback))
    {
        *ppvInterface = this;
        AddRef();
        return S_OK;
    }
    *ppvInterface = nullptr;
    return E_NOINTERFACE;
}

// IAudioEndpointVolumeCallback method
STDMETHODIMP VolumeMirror::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
{
    if (!pNotify)
        return E_POINTER;

    std::lock_guard<std::mutex> lock(stateMutex);

    float currentVolume = scalarToPercent(pNotify->fMasterVolume);
    bool isMuted = (pNotify->bMuted != FALSE);

    // Process Windows volume changes only if they are significant
    if (fabs(currentVolume - lastWindowsVolume) > 1.0f || isMuted != lastWindowsMute)
    {
        if (!ignoreWindowsChange)
        {
            float mappedVolume = percentToDbm(currentVolume);
            ignoreVoicemeeterChange = true; // Prevent recursion in Voicemeeter

            HRESULT hr;
            if (channelType == ChannelType::Output)
            {
                hr = vmrAPI->SetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Gain").c_str(), mappedVolume);
                if (FAILED(hr)) {
                    Log("Failed to set Voicemeeter Bus Gain.");
                }
                hr = vmrAPI->SetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Mute").c_str(), isMuted ? 1.0f : 0.0f);
                if (FAILED(hr)) {
                    Log("Failed to set Voicemeeter Bus Mute.");
                }
            }
            else if (channelType == ChannelType::Input)
            {
                hr = vmrAPI->SetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Gain").c_str(), mappedVolume);
                if (FAILED(hr)) {
                    Log("Failed to set Voicemeeter Strip Gain.");
                }
                hr = vmrAPI->SetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Mute").c_str(), isMuted ? 1.0f : 0.0f);
                if (FAILED(hr)) {
                    Log("Failed to set Voicemeeter Strip Mute.");
                }
            }

            lastWindowsVolume = currentVolume;
            lastWindowsMute = isMuted;
        }
        else
        {
            ignoreWindowsChange = false;
        }
    }

    return S_OK;
}




void VolumeMirror::MonitorVoicemeeter()
{
    while (running)
    {
        try
        {
            if (vmrAPI->IsParametersDirty() > 0)
            {
                std::lock_guard<std::mutex> lock(stateMutex);

                float vmGain = 0.0f;
                bool vmMute = false;

                if (channelType == ChannelType::Output)
                {
                    if (vmrAPI->GetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Gain").c_str(), &vmGain) != 0)
                        vmGain = minDbm;

                    float muteVal = 0.0f;
                    if (vmrAPI->GetParameterFloat(("Bus[" + std::to_string(channelIndex) + "].Mute").c_str(), &muteVal) == 0) {
                        vmMute = (muteVal != 0.0f);
                    }
                }
                else if (channelType == ChannelType::Input)
                {
                    if (vmrAPI->GetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Gain").c_str(), &vmGain) != 0)
                        vmGain = minDbm;

                    float muteVal = 0.0f;
                    if (vmrAPI->GetParameterFloat(("Strip[" + std::to_string(channelIndex) + "].Mute").c_str(), &muteVal) == 0) {
                        vmMute = (muteVal != 0.0f);
                    }
                }

                float mappedVmVolume = dBmToPercent(vmGain);

                if (!ignoreVoicemeeterChange && (fabs(mappedVmVolume - lastVmVolume) > 1.0f || vmMute != lastVmMute))
                {
                    ignoreWindowsChange = true; // Prevent recursion in Windows volume updates

                    float volumeScalar = percentToScalar(mappedVmVolume);
                    HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(volumeScalar, NULL);
                    if (FAILED(hr))
                    {
                        Log("Failed to set Windows volume.");
                    }
                    hr = endpointVolume->SetMute(vmMute, NULL);
                    if (FAILED(hr))
                    {
                        Log("Failed to set Windows mute state.");
                    }

                    lastChangeTime = std::chrono::steady_clock::now();
                    playSoundOnSync = true;

                    lastVmVolume = mappedVmVolume;
                    lastVmMute = vmMute;
                }
                else
                {
                    ignoreVoicemeeterChange = false;
                }
            }

            if (playSoundOnSync && (std::chrono::steady_clock::now() - lastChangeTime > std::chrono::milliseconds(200)))
            {
                PlaySound(TEXT("C:/Windows/Media/Windows Unlock.wav"), NULL, SND_FILENAME | SND_ASYNC);
                playSoundOnSync = false;
            }

            //std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        catch (const std::exception& ex)
        {
            Log(std::string("Error in Voicemeeter monitoring: ") + ex.what());
        }
    }
}



// dBmToPercent method
float VolumeMirror::dBmToPercent(float dbm)
{
    if (dbm < minDbm)
        dbm = minDbm;
    if (dbm > maxDbm)
        dbm = maxDbm;
    return ((dbm - minDbm) / (maxDbm - minDbm)) * 100.0f;
}

// percentToScalar method
float VolumeMirror::percentToScalar(float percent)
{
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 100.0f)
        percent = 100.0f;
    return percent / 100.0f;
}

// scalarToPercent method
float VolumeMirror::scalarToPercent(float scalar)
{
    if (scalar < 0.0f)
        scalar = 0.0f;
    if (scalar > 1.0f)
        scalar = 1.0f;
    return scalar * 100.0f;
}

// percentToDbm method
float VolumeMirror::percentToDbm(float percent)
{
    return (percent / 100.0f) * (maxDbm - minDbm) + minDbm;
}

// Logging method
void VolumeMirror::Log(const std::string& message)
{
    if (debug)
    {
        std::cout << "[DEBUG] " << message << std::endl;
    }
}
