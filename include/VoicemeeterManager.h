#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include "RAIIHandle.h"
#include "Defconf.h"


// Type definition for callback identifiers
using CallbackID = unsigned int;

// Forward declaration of RAIIHMODULE (assuming it's defined in RAIIHandle.h)
class RAIIHMODULE;

/**
 * @brief Manages interactions with the Voicemeeter Remote API.
 *
 * The VoicemeeterManager class provides an interface to control and monitor
 * Voicemeeter applications via the VoicemeeterRemote.dll. It handles
 * initialization, parameter management, device selection, and callback
 * registration for volume changes.
 */
class VoicemeeterManager {
public:
    /**
     * @brief Constructs a new VoicemeeterManager object.
     *
     * Initializes member variables and sets up the initial state.
     */
    VoicemeeterManager();

    /**
     * @brief Destructs the VoicemeeterManager object.
     *
     * Ensures that all resources are properly released and the connection
     * to Voicemeeter is gracefully terminated.
     */
    ~VoicemeeterManager();

    /**
     * @brief Initializes the VoicemeeterManager with the specified Voicemeeter type.
     *
     * Attempts to load the VoicemeeterRemote DLL, log in to Voicemeeter, and
     * set up the A1 device if necessary.
     *
     * @param voicemeeterType The type of Voicemeeter application to initialize.
     * @return true if initialization is successful, false otherwise.
     */
    bool Initialize(int voicemeeterType);

    /**
     * @brief Shuts down the VoicemeeterManager, logging out and unloading the DLL.
     *
     * This method should be called to gracefully terminate the connection
     * with Voicemeeter and release all associated resources.
     */
    void Shutdown();

    /**
     * @brief Sends a shutdown command to Voicemeeter.
     *
     * This command instructs Voicemeeter to terminate its execution.
     */
    void ShutdownCommand();

    /**
     * @brief Restarts the Voicemeeter audio engine with specified delays.
     *
     * @param beforeRestartDelay Delay in seconds before sending the restart command.
     * @param afterRestartDelay Delay in seconds after sending the restart command.
     */
    void RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay);

    /**
     * @brief Lists all input and output channels in Voicemeeter.
     *
     * Retrieves and logs information about all input strips and output buses.
     */
    void ListAllChannels();

    /**
     * @brief Lists all virtual input channels in Voicemeeter.
     *
     * Retrieves and logs information about available virtual inputs.
     */
    void ListInputs();

    /**
     * @brief Lists all virtual output channels in Voicemeeter.
     *
     * Retrieves and logs information about available virtual outputs.
     */
    void ListOutputs();

    /**
     * @brief Retrieves the volume and mute state of a specified channel.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @param volumePercent Reference to store the volume percentage.
     * @param isMuted Reference to store the mute state.
     * @return true if retrieval is successful, false otherwise.
     */
    bool GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted);

    /**
     * @brief Updates the volume and mute state of a specified channel.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @param volumePercent The desired volume percentage.
     * @param isMuted The desired mute state.
     */
    void UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted);

    /**
     * @brief Checks if Voicemeeter parameters have changed since the last check.
     *
     * @return true if parameters are dirty (changed), false otherwise.
     */
    bool IsParametersDirty();

    /**
     * @brief Retrieves the volume percentage of a specified channel.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @param volumePercent Reference to store the volume percentage.
     * @return true if retrieval is successful, false otherwise.
     */
    bool GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent);

    /**
     * @brief Checks if a specified channel is muted.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @return true if the channel is muted, false otherwise.
     */
    bool IsChannelMuted(int channelIndex, ChannelType channelType);

    /**
     * @brief Sets the mute state of a specified channel.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @param isMuted The desired mute state.
     * @return true if the operation is successful, false otherwise.
     */
    bool SetMute(int channelIndex, ChannelType channelType, bool isMuted);

    /**
     * @brief Registers a callback function to be invoked on volume changes.
     *
     * @param callback A std::function taking volume percentage and mute state as parameters.
     * @return A unique CallbackID for the registered callback.
     */
    CallbackID RegisterVolumeChangeCallback(std::function<void(float, bool)> callback);

    /**
     * @brief Unregisters a previously registered volume change callback.
     *
     * @param callbackID The CallbackID of the callback to unregister.
     * @return true if unregistration is successful, false otherwise.
     */
    bool UnregisterVolumeChangeCallback(CallbackID callbackID);

private:
    /**
     * @brief Loads the VoicemeeterRemote DLL and initializes function pointers.
     *
     * @return true if the DLL is loaded and function pointers are initialized successfully, false otherwise.
     */
    bool LoadVoicemeeterRemote();

    /**
     * @brief Unloads the VoicemeeterRemote DLL and resets function pointers.
     */
    void UnloadVoicemeeterRemote();

    /**
     * @brief Retrieves the first available WDM device name.
     *
     * @return The name of the first WDM device found, or an empty string if none are found.
     */
    std::string GetFirstWdmDeviceName();

    /**
     * @brief Sets the A1 device to the specified device name.
     *
     * @param deviceName The name of the WDM device to set as A1.
     * @return true if the device is set successfully, false otherwise.
     */
    bool SetA1Device(const std::string& deviceName);

    /**
     * @brief Internal method to set the mute state of a channel.
     *
     * @param channelIndex The index of the channel.
     * @param channelType The type of the channel (Input or Bus).
     * @param isMuted The desired mute state.
     * @return true if the operation is successful, false otherwise.
     */
    bool SetMuteInternal(int channelIndex, ChannelType channelType, bool isMuted);

    // Function pointer typedefs for VoicemeeterRemote DLL functions
    typedef long(__stdcall* T_VBVMR_Login)();
    typedef long(__stdcall* T_VBVMR_Logout)();
    typedef long(__stdcall* T_VBVMR_RunVoicemeeter)(int type);
    typedef long(__stdcall* T_VBVMR_GetVoicemeeterType)(long* type);
    typedef long(__stdcall* T_VBVMR_GetVoicemeeterVersion)(char* buffer);
    typedef long(__stdcall* T_VBVMR_IsParametersDirty)();
    typedef long(__stdcall* T_VBVMR_GetParameterFloat)(char* param, float* value);
    typedef long(__stdcall* T_VBVMR_GetParameterStringA)(char* param, char* buffer);
    typedef long(__stdcall* T_VBVMR_GetParameterStringW)(wchar_t* param, wchar_t* buffer);
    typedef long(__stdcall* T_VBVMR_SetParameterFloat)(char* param, float value);
    typedef long(__stdcall* T_VBVMR_SetParameterStringA)(char* param, const char* value);
    typedef long(__stdcall* T_VBVMR_SetParameters)(const char* params);
    typedef long(__stdcall* T_VBVMR_Output_GetDeviceNumber)();
    typedef long(__stdcall* T_VBVMR_Output_GetDeviceDescA)(int index, int* type, char* name, char* hwId);

    // Function pointers for VoicemeeterRemote DLL
    T_VBVMR_Login VBVMR_Login;
    T_VBVMR_Logout VBVMR_Logout;
    T_VBVMR_RunVoicemeeter VBVMR_RunVoicemeeter;
    T_VBVMR_GetVoicemeeterType VBVMR_GetVoicemeeterType;
    T_VBVMR_GetVoicemeeterVersion VBVMR_GetVoicemeeterVersion;
    T_VBVMR_IsParametersDirty VBVMR_IsParametersDirty;
    T_VBVMR_GetParameterFloat VBVMR_GetParameterFloat;
    T_VBVMR_GetParameterStringA VBVMR_GetParameterStringA;
    T_VBVMR_GetParameterStringW VBVMR_GetParameterStringW;
    T_VBVMR_SetParameterFloat VBVMR_SetParameterFloat;
    T_VBVMR_SetParameterStringA VBVMR_SetParameterStringA;
    T_VBVMR_SetParameters VBVMR_SetParameters;
    T_VBVMR_Output_GetDeviceNumber VBVMR_Output_GetDeviceNumber;
    T_VBVMR_Output_GetDeviceDescA VBVMR_Output_GetDeviceDescA;

    // RAII handle for the VoicemeeterRemote DLL
    RAIIHMODULE hVoicemeeterRemote;

    // Initialization and login state
    bool initialized;
    bool loggedIn;

    // Mutexes for thread safety
    std::mutex initMutex_;
    std::mutex shutdownMutex_;
    std::mutex channelMutex_;
    std::mutex callbackMutex_;

    // Callback management
    std::map<CallbackID, std::function<void(float, bool)>> volumeChangeCallbacks_;
    CallbackID nextCallbackID_;

    // Constants (define these appropriately or ensure they are defined elsewhere)
    static constexpr int DEFAULT_STARTUP_DELAY_MS = 5000; // Example value
    static constexpr int MAX_RETRIES = 10;               // Example value
    static constexpr int RETRY_DELAY_MS = 1000;          // Example value
};

