// VolumeMirror.h

#ifndef VOLUMEMIRROR_H
#define VOLUMEMIRROR_H

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <wrl/client.h>
#include "VoicemeeterManager.h"

// Forward declaration of the AUDIO_VOLUME_NOTIFICATION_DATA structure
struct AUDIO_VOLUME_NOTIFICATION_DATA;

// Enum to specify the type of channel
enum class ChannelType
{
    Input,
    Output
};

// VolumeMirror Class Definition
class VolumeMirror : public IAudioEndpointVolumeCallback
{
public:
    /**
     * @brief Constructor for VolumeMirror.
     * @param channelIdx Index of the Voicemeeter channel to synchronize.
     * @param type Type of the channel (Input or Output).
     * @param minDbmVal Minimum dBm value for volume conversion.
     * @param maxDbmVal Maximum dBm value for volume conversion.
     * @param manager Reference to the VoicemeeterManager instance.
     * @param playSound Flag to indicate whether to play a sound on synchronization.
     */
    VolumeMirror(int channelIdx, ChannelType type, float minDbmVal, float maxDbmVal,
                 VoicemeeterManager& manager, bool playSound);

    /**
     * @brief Destructor for VolumeMirror.
     */
    ~VolumeMirror();

    /**
     * @brief Starts the volume mirroring process.
     */
    void Start();

    /**
     * @brief Stops the volume mirroring process.
     */
    void Stop();

    /**
     * @brief Implementation of IAudioEndpointVolumeCallback's OnNotify method.
     * @param pNotify Pointer to AUDIO_VOLUME_NOTIFICATION_DATA structure.
     * @return HRESULT indicating success or failure.
     */
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

    /**
     * @brief Implementation of IUnknown's QueryInterface method.
     * @param riid Reference to the identifier of the interface.
     * @param ppvObject Pointer to the interface pointer.
     * @return HRESULT indicating success or failure.
     */
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;

    /**
     * @brief Implementation of IUnknown's AddRef method.
     * @return ULONG representing the new reference count.
     */
    STDMETHODIMP_(ULONG) AddRef() override;

    /**
     * @brief Implementation of IUnknown's Release method.
     * @return ULONG representing the new reference count.
     */
    STDMETHODIMP_(ULONG) Release() override;

     /**
     * @brief Sets the polling mode and interval.
     * @param enabled Whether polling is enabled.
     * @param interval Polling interval in milliseconds.
     */
    void SetPollingMode(bool enabled, int interval);
    GUID eventContextGuid;


private:
    // Prevent copying and assignment
    VolumeMirror(const VolumeMirror&) = delete;
    VolumeMirror& operator=(const VolumeMirror&) = delete;

    /**
     * @brief Initializes the Voicemeeter channel's initial state.
     */
    void InitializeVoicemeeterState();

    /**
     * @brief Monitors Voicemeeter parameters in a separate thread.
     */
    void MonitorVoicemeeter();

    /**
     * @brief Retrieves the current Voicemeeter volume and mute state.
     * @param volumePercent Reference to store the volume percentage (0.0f - 100.0f).
     * @param isMuted Reference to store the mute state (true if muted).
     * @return True if successful, false otherwise.
     */
    bool GetVoicemeeterVolume(float& volumePercent, bool& isMuted);

    /**
     * @brief Updates Voicemeeter's volume and mute state based on Windows volume.
     * @param volumePercent Volume percentage (0.0f - 100.0f).
     * @param isMuted Mute state (true if muted).
     */
    void UpdateVoicemeeterVolume(float volumePercent, bool isMuted);

    /**
     * @brief Updates Windows' volume and mute state based on Voicemeeter volume.
     * @param volumePercent Volume percentage (0.0f - 100.0f).
     * @param isMuted Mute state (true if muted).
     */
    void UpdateWindowsVolume(float volumePercent, bool isMuted);

    /**
     * @brief Converts scalar volume (0.0f - 1.0f) to percentage (0.0f - 100.0f).
     * @param scalar Scalar volume value.
     * @return Volume as a percentage.
     */
    float scalarToPercent(float scalar);

    /**
     * @brief Converts percentage volume (0.0f - 100.0f) to scalar (0.0f - 1.0f).
     * @param percent Percentage volume value.
     * @return Scalar volume value.
     */
    float percentToScalar(float percent);

    /**
     * @brief Converts dBm gain value to percentage volume based on min and max dBm.
     * @param dBm dBm gain value.
     * @return Volume as a percentage.
     */
    float dBmToPercent(float dBm);

    /**
     * @brief Converts percentage volume (0.0f - 100.0f) to dBm gain value based on min and max dBm.
     * @param percent Percentage volume value.
     * @return dBm gain value.
     */
    float percentToDbm(float percent);

    /**
     * @brief Plays a synchronization sound asynchronously.
     */
    void PlaySyncSound();

    /**
     * @brief Checks if two floating-point numbers are equal within a tolerance.
     * @param a First float.
     * @param b Second float.
     * @param epsilon Tolerance value.
     * @return True if equal within tolerance, else false.
     */
    bool IsFloatEqual(float a, float b, float epsilon = 0.01f);

    // Member Variables

    // Voicemeeter Channel Information
    int channelIndex;
    ChannelType channelType;

    // Volume Conversion Parameters
    float minDbm;
    float maxDbm;

    // Voicemeeter Manager and API
    VoicemeeterManager& voicemeeterManager;
    VoicemeeterAPI* vmrAPI;

    // COM Interfaces using ComPtr for automatic management
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    Microsoft::WRL::ComPtr<IMMDevice> speakers;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume;

    // Volume States
    std::atomic<float> lastWindowsVolume;
    std::atomic<bool> lastWindowsMute;
    std::atomic<float> lastVmVolume;
    std::atomic<bool> lastVmMute;

    // Synchronization Flags
    std::atomic<bool> ignoreWindowsChange;
    std::atomic<bool> ignoreVoicemeeterChange;

    // Control Flags
    std::atomic<bool> running;

    // Reference Count for IUnknown
    std::atomic<ULONG> refCount;

    // Callback Control
    bool playSoundOnSync;

    std::chrono::steady_clock::time_point lastSoundPlayTime;
    std::chrono::milliseconds soundCooldownDuration;
    std::chrono::milliseconds debounceDuration;

    std::mutex soundMutex;

    // Mutexes for Thread Safety
    std::mutex vmMutex;        // Protects volume synchronization
    std::mutex controlMutex;   // Protects start/stop control

    // Monitoring Thread
    std::thread monitorThread;

    // Polling settings
    bool pollingEnabled;
    int pollingInterval;



};

#endif // VOLUMEMIRROR_H
