#pragma once
#include <Windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "Defconf.h"
#include "RAIIHandle.h"

class VoicemeeterManager {
   public:
    VoicemeeterManager();
    ~VoicemeeterManager();

    VoicemeeterManager(const VoicemeeterManager&) = delete;
    VoicemeeterManager& operator=(const VoicemeeterManager&) = delete;
    VoicemeeterManager(VoicemeeterManager&&) = delete;
    VoicemeeterManager& operator=(VoicemeeterManager&&) = delete;

    bool Initialize(int voicemeeterType);
    void Shutdown();
    void ShutdownCommand();
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);

    bool SetMute(int channelIndex, ChannelType channelType, bool isMuted);
    bool GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent);
    bool IsChannelMuted(int channelIndex, ChannelType channelType);
    bool GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted);
    void UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted);

    bool IsParametersDirty();

    void ListAllChannels();
    void ListInputs();
    void ListOutputs();

    std::mutex toggleMutex;
    std::mutex channelMutex;

   private:
    bool LoadVoicemeeterRemote();
    void UnloadVoicemeeterRemote();
    RAIIHMODULE hVoicemeeterRemote;

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
    typedef long(__stdcall* T_VBVMR_SetParameterStringA)(char* szParamName, const char* value);
    typedef long(__stdcall* T_VBVMR_SetParameters)(const char* paramScript);
    typedef long(__stdcall* T_VBVMR_Output_GetDeviceNumber)(void);
    typedef long(__stdcall* T_VBVMR_Output_GetDeviceDescA)(int index, int* type, char* name, char* hardwareId);

    // Function pointer declarations
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

    bool initialized;
    bool loggedIn;
    bool debugMode;

    // Helper methods
    std::string GetFirstWdmDeviceName();
    bool SetA1Device(const std::string& deviceName);
};