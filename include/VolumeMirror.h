// VolumeMirror.h
#ifndef VOLUMEMIRROR_H
#define VOLUMEMIRROR_H

#include <windows.h>
#include <sapi.h> // For PlaySound
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "VolumeUtils.h"
#include "VoicemeeterManager.h"

/**
 * @brief The VolumeMirror class synchronizes volume and mute states between Windows
 *        and Voicemeeter channels.
 *
 * This class listens for volume changes in Windows and updates the corresponding
 * Voicemeeter channel. It can operate in polling mode to detect changes from
 * Voicemeeter and mirror them to Windows. Additionally, it provides auditory
 * feedback through synchronization sounds.
 */
class VolumeMirror : public IAudioEndpointVolumeCallback {
public:
    /**
     * @brief Constructor for VolumeMirror.
     *
     * Initializes the VolumeMirror instance, sets up Windows audio components,
     * and retrieves the initial volume and mute states from VoicemeeterManager.
     *
     * @param channelIdx Index of the Voicemeeter channel to mirror.
     * @param type Type of the channel (Input or Output).
     * @param minDbmVal Minimum dBm value for volume adjustments.
     * @param maxDbmVal Maximum dBm value for volume adjustments.
     * @param manager Reference to an instance of VoicemeeterManager.
     * @param playSound Whether to play a synchronization sound on changes.
     *
     * @throws std::runtime_error If initialization of Windows audio components fails.
     */
    VolumeMirror(int channelIdx, VolumeUtils::ChannelType type, float minDbmVal,
                float maxDbmVal, VoicemeeterManager &manager, bool playSound);
    
    /**
     * @brief Destructor for VolumeMirror.
     *
     * Stops mirroring, unregisters for volume change notifications,
     * and cleans up resources.
     */
    ~VolumeMirror();

    /**
     * @brief Starts the volume mirroring process.
     *
     * Begins monitoring for volume changes and synchronizing between
     * Windows and Voicemeeter.
     */
    void Start();

    /**
     * @brief Stops the volume mirroring process.
     *
     * Halts monitoring for volume changes and terminates any active threads.
     */
    void Stop();

    /**
     * @brief Sets the polling mode and interval for monitoring.
     *
     * @param enabled Whether to enable polling mode.
     * @param interval Polling interval in milliseconds.
     */
    void SetPollingMode(bool enabled, int interval);

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback method
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;

private:
    /**
     * @brief Monitors Voicemeeter parameters in a separate thread.
     *
     * Continuously checks for changes in Voicemeeter's volume and mute states
     * and updates Windows settings accordingly.
     */
    void MonitorVoicemeeter();

    /**
     * @brief Retrieves the current Voicemeeter volume and mute state.
     *
     * @param volumePercent Reference to store the volume percentage.
     * @param isMuted Reference to store the mute state.
     * @return True if successful, false otherwise.
     */
    bool GetVoicemeeterVolume(float &volumePercent, bool &isMuted);

    /**
     * @brief Updates the Voicemeeter volume and mute state based on Windows settings.
     *
     * @param volumePercent Volume percentage to set in Voicemeeter.
     * @param isMuted Mute state to set in Voicemeeter.
     */
    void UpdateVoicemeeterVolume(float volumePercent, bool isMuted);

    /**
     * @brief Updates the Windows volume and mute state based on Voicemeeter settings.
     *
     * @param volumePercent Volume percentage to set in Windows.
     * @param isMuted Mute state to set in Windows.
     */
    void UpdateWindowsVolume(float volumePercent, bool isMuted);

    /**
     * @brief Plays a synchronization sound to indicate volume changes.
     *
     * Attempts to play a custom sound and falls back to a system sound if the custom sound fails.
     */
    void PlaySyncSound();

    // Member variables
    int channelIndex;                                 ///< Index of the Voicemeeter channel.
    VolumeUtils::ChannelType channelType;             ///< Type of the channel (Input or Output).
    float minDbm;                                     ///< Minimum dBm value for volume.
    float maxDbm;                                     ///< Maximum dBm value for volume.
    VoicemeeterManager &vmManager;                    ///< Reference to VoicemeeterManager.

    float lastWindowsVolume;                          ///< Last known Windows volume percentage.
    bool lastWindowsMute;                             ///< Last known Windows mute state.
    float lastVmVolume;                               ///< Last known Voicemeeter volume percentage.
    bool lastVmMute;                                  ///< Last known Voicemeeter mute state.

    bool ignoreWindowsChange;                         ///< Flag to ignore Windows-initiated changes.
    bool ignoreVoicemeeterChange;                     ///< Flag to ignore Voicemeeter-initiated changes.
    bool running;                                     ///< Flag indicating if mirroring is active.

    std::atomic<ULONG> refCount;                      ///< Reference count for COM.
    bool playSoundOnSync;                             ///< Flag to play sound on synchronization.

    bool pollingEnabled;                              ///< Flag to enable polling mode.
    int pollingInterval;                              ///< Polling interval in milliseconds.
    std::chrono::steady_clock::time_point lastSoundPlayTime; ///< Last time a sound was played.
    std::chrono::milliseconds debounceDuration;       ///< Debounce duration to prevent rapid sound playback.

    // COM interfaces for Windows Audio
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator; ///< COM pointer to MMDeviceEnumerator.
    Microsoft::WRL::ComPtr<IMMDevice> speakers;                    ///< COM pointer to the default speakers.
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpointVolume;   ///< COM pointer to the audio endpoint volume.

    // Thread and synchronization
    std::thread monitorThread;       ///< Thread for monitoring Voicemeeter changes.
    std::mutex controlMutex;         ///< Mutex to protect the running flag.
    std::mutex vmMutex;              ///< Mutex to protect Voicemeeter-related changes.
    std::mutex soundMutex;           ///< Mutex to protect sound playback.

    GUID eventContextGuid = {0};     ///< GUID to identify the event context.

    // Disable copy and assignment
    VolumeMirror(const VolumeMirror &) = delete;
    VolumeMirror &operator=(const VolumeMirror &) = delete;
};

#endif // VOLUMEMIRROR_H
