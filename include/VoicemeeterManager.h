#ifndef VOICEMEETER_MANAGER_H
#define VOICEMEETER_MANAGER_H

#pragma once

#include "RAIIHandle.h"
#include <functional>
#include <vector>
#include <mutex>
#include "Defconf.h"


/**
 * @brief Manages interactions with the Voicemeeter API.
 *
 * This class handles initialization, communication, and control of the Voicemeeter application
 * using the Voicemeeter Remote API.
 */
class VoicemeeterManager {
public:
    VoicemeeterManager();
    ~VoicemeeterManager();

    VoicemeeterManager(const VoicemeeterManager&) = delete;
    VoicemeeterManager& operator=(const VoicemeeterManager&) = delete;
    VoicemeeterManager(VoicemeeterManager&&) = delete;
    VoicemeeterManager& operator=(VoicemeeterManager&&) = delete;
   
    // Initialization and Shutdown
    bool Initialize(int voicemeeterType);
    void Shutdown();
    void ShutdownCommand();
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);

    // Debug Mode
    void SetDebugMode(bool newDebugMode);
    bool GetDebugMode();

    // Channel Operations
    bool SetMute(int channelIndex, ChannelType channelType, bool isMuted);
    bool GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent);
    bool IsChannelMuted(int channelIndex, ChannelType channelType);
    bool GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted);
    void UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted);

    // Parameter Checks
    bool IsParametersDirty();

    // Listing Functions
    void ListAllChannels();
    void ListInputs();
    void ListOutputs();

    // Mutexes for thread safety
    std::mutex toggleMutex;
    std::mutex channelMutex;

private:
    // Voicemeeter Remote DLL Management
    bool LoadVoicemeeterRemote();
    void UnloadVoicemeeterRemote();

    // Internal Mute Handling
    bool SetMuteInternal(int channelIndex, ChannelType channelType, bool isMuted);

    // Function pointer typedefs for Voicemeeter Remote API
    typedef long(__stdcall* T_VBVMR_Login)(void);
    typedef long(__stdcall* T_VBVMR_Logout)(void);
    typedef long(__stdcall* T_VBVMR_RunVoicemeeter)(long vType);
    typedef long(__stdcall* T_VBVMR_GetVoicemeeterType)(long* pType);
    typedef long(__stdcall* T_VBVMR_GetVoicemeeterVersion)(long* pVersion);
    typedef long(__stdcall* T_VBVMR_IsParametersDirty)(void);
    typedef long(__stdcall* T_VBVMR_GetParameterFloat)(char* szParamName, float* pValue);
    typedef long(__stdcall* T_VBVMR_GetParameterStringA)(char* szParamName, char* szString);
    typedef long(__stdcall* T_VBVMR_GetParameterStringW)(char* szParamName, unsigned short* wszString);
    typedef long(__stdcall* T_VBVMR_SetParameterFloat)(char* szParamName, float Value);

    // Voicemeeter Remote DLL handle
    RAIIHMODULE hVoicemeeterRemote;

    // Function pointers
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

    // Initialization flags
    bool initialized;
    bool loggedIn;
    bool debugMode;
};

#endif // VOICEMEETER_MANAGER_H
