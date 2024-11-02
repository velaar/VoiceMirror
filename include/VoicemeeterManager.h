#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <windows.h>
#include <functional>

// Include necessary COM and audio headers
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include "VolumeUtils.h"
#include "Logger.h"

// Forward declarations for COM interfaces
using Microsoft::WRL::ComPtr;

/**
 * @brief Struct for holding volume and mute state
 */
struct ChannelState {
    float volume;
    bool isMuted;
};

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

    bool Initialize(int voicemeeterType);
    void Shutdown();
    void ShutdownCommand();
    bool SetMute(int channelIndex, VolumeUtils::ChannelType channelType, bool isMuted);
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);
    void SetDebugMode(bool newDebugMode);
    bool GetDebugMode();
    void ListAllChannels();
    void ListInputs();
    void ListOutputs();
    bool GetVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float &volumePercent, bool &isMuted);
    void UpdateVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float volumePercent, bool isMuted);
    bool IsParametersDirty();
    void ListMonitorableDevices();
    bool InitializeVoicemeeterState(int channelIndex, VolumeUtils::ChannelType channelType);
    float GetChannelVolume(int channelIndex, VolumeUtils::ChannelType channelType);
    bool IsChannelMuted(int channelIndex, VolumeUtils::ChannelType channelType);

private:
    bool InitializeCOM();
    void UninitializeCOM();
    bool LoadVoicemeeterRemote();
    void UnloadVoicemeeterRemote();
    bool SetMuteInternal(int channelIndex, VolumeUtils::ChannelType channelType, bool isMuted);

    // Function pointer typedefs for Voicemeeter Remote API
    typedef long (__stdcall *T_VBVMR_Login)(void);
    typedef long (__stdcall *T_VBVMR_Logout)(void);
    typedef long (__stdcall *T_VBVMR_RunVoicemeeter)(long vType);
    typedef long (__stdcall *T_VBVMR_GetVoicemeeterType)(long* pType);
    typedef long (__stdcall *T_VBVMR_GetVoicemeeterVersion)(long* pVersion);
    typedef long (__stdcall *T_VBVMR_IsParametersDirty)(void);
    typedef long (__stdcall *T_VBVMR_GetParameterFloat)(char* szParamName, float* pValue);
    typedef long (__stdcall *T_VBVMR_GetParameterStringA)(char* szParamName, char* szString);
    typedef long (__stdcall *T_VBVMR_GetParameterStringW)(char* szParamName, unsigned short* wszString);
    typedef long (__stdcall *T_VBVMR_GetLevel)(long nType, long nuChannel, float* pValue);
    typedef long (__stdcall *T_VBVMR_GetMidiMessage)(unsigned char* pMIDIBuffer, long nbByteMax);
    typedef long (__stdcall *T_VBVMR_SendMidiMessage)(unsigned char* pMIDIBuffer, long nbByteMax);
    typedef long (__stdcall *T_VBVMR_SetParameterFloat)(char* szParamName, float Value);
    typedef long (__stdcall *T_VBVMR_SetParameters)(char* szParamScript);
    typedef long (__stdcall *T_VBVMR_SetParametersW)(unsigned short* szParamScript);
    typedef long (__stdcall *T_VBVMR_SetParameterStringA)(char* szParamName, char* szString);
    typedef long (__stdcall *T_VBVMR_SetParameterStringW)(char* szParamName, unsigned short* wszString);
    typedef long (__stdcall *T_VBVMR_Output_GetDeviceNumber)(void);
    typedef long (__stdcall *T_VBVMR_Output_GetDeviceDescA)(long zindex, long* nType, char* szDeviceName, char* szHardwareId);
    typedef long (__stdcall *T_VBVMR_Output_GetDeviceDescW)(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId);
    typedef long (__stdcall *T_VBVMR_Input_GetDeviceNumber)(void);
    typedef long (__stdcall *T_VBVMR_Input_GetDeviceDescA)(long zindex, long* nType, char* szDeviceName, char* szHardwareId);
    typedef long (__stdcall *T_VBVMR_Input_GetDeviceDescW)(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId);
    typedef long (__stdcall *T_VBVMR_AudioCallbackRegister)(long mode, std::function<void(void*, long, void*, long)> pCallback, void* lpUser, char szClientName[64]);
    typedef long (__stdcall *T_VBVMR_AudioCallbackStart)(void);
    typedef long (__stdcall *T_VBVMR_AudioCallbackStop)(void);
    typedef long (__stdcall *T_VBVMR_AudioCallbackUnregister)(void);
    typedef long (__stdcall *T_VBVMR_MacroButton_IsDirty)(void);
    typedef long (__stdcall *T_VBVMR_MacroButton_GetStatus)(long nuLogicalButton, float* pValue, long bitmode);
    typedef long (__stdcall *T_VBVMR_MacroButton_SetStatus)(long nuLogicalButton, float fValue, long bitmode);

    // Voicemeeter Remote DLL handle
    HMODULE hVoicemeeterRemote;

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
    T_VBVMR_GetLevel VBVMR_GetLevel;
    T_VBVMR_GetMidiMessage VBVMR_GetMidiMessage;
    T_VBVMR_SendMidiMessage VBVMR_SendMidiMessage;
    T_VBVMR_SetParameterFloat VBVMR_SetParameterFloat;
    T_VBVMR_SetParameters VBVMR_SetParameters;
    T_VBVMR_SetParametersW VBVMR_SetParametersW;
    T_VBVMR_SetParameterStringA VBVMR_SetParameterStringA;
    T_VBVMR_SetParameterStringW VBVMR_SetParameterStringW;
    T_VBVMR_Output_GetDeviceNumber VBVMR_Output_GetDeviceNumber;
    T_VBVMR_Output_GetDeviceDescA VBVMR_Output_GetDeviceDescA;
    T_VBVMR_Output_GetDeviceDescW VBVMR_Output_GetDeviceDescW;
    T_VBVMR_Input_GetDeviceNumber VBVMR_Input_GetDeviceNumber;
    T_VBVMR_Input_GetDeviceDescA VBVMR_Input_GetDeviceDescA;
    T_VBVMR_Input_GetDeviceDescW VBVMR_Input_GetDeviceDescW;
    T_VBVMR_AudioCallbackRegister VBVMR_AudioCallbackRegister;
    T_VBVMR_AudioCallbackStart VBVMR_AudioCallbackStart;
    T_VBVMR_AudioCallbackStop VBVMR_AudioCallbackStop;
    T_VBVMR_AudioCallbackUnregister VBVMR_AudioCallbackUnregister;
    T_VBVMR_MacroButton_IsDirty VBVMR_MacroButton_IsDirty;
    T_VBVMR_MacroButton_GetStatus VBVMR_MacroButton_GetStatus;
    T_VBVMR_MacroButton_SetStatus VBVMR_MacroButton_SetStatus;

    // Initialization flags
    bool initialized;
    bool loggedIn;
    bool debugMode;
    bool comInitialized;

    // Mutexes for thread safety
    std::mutex toggleMutex;
    std::mutex comInitMutex; // Added to handle COM initialization thread-safely
    std::mutex channelStatesMutex;

    // Channel states
    std::unordered_map<int, ChannelState> channelStates;
};
