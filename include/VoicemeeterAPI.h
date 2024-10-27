// include/VoicemeeterAPI.h

#ifndef VOICEMETER_API_H
#define VOICEMETER_API_H

#include <windows.h>
#include "VoicemeeterRemote.h" // Ensure VoicemeeterRemote.h is placed appropriately

// Function pointer typedefs as defined in VoicemeeterRemote.h
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

typedef long (__stdcall *T_VBVMR_AudioCallbackRegister)(long mode, T_VBVMR_VBAUDIOCALLBACK pCallback, void* lpUser, char szClientName[64]);
typedef long (__stdcall *T_VBVMR_AudioCallbackStart)(void);
typedef long (__stdcall *T_VBVMR_AudioCallbackStop)(void);
typedef long (__stdcall *T_VBVMR_AudioCallbackUnregister)(void);

typedef long (__stdcall *T_VBVMR_MacroButton_IsDirty)(void);
typedef long (__stdcall *T_VBVMR_MacroButton_GetStatus)(long nuLogicalButton, float* pValue, long bitmode);
typedef long (__stdcall *T_VBVMR_MacroButton_SetStatus)(long nuLogicalButton, float fValue, long bitmode);

// Voicemeeter API Wrapper Class
class VoicemeeterAPI {
public:
    VoicemeeterAPI();
    ~VoicemeeterAPI();

    bool Initialize();
    void Shutdown();

    // API Functions
    long Login();
    long Logout();
    long RunVoicemeeter(long vType);

    long GetVoicemeeterType(long* pType);
    long GetVoicemeeterVersion(long* pVersion);

    long IsParametersDirty();
    long GetParameterFloat(const char* param, float* value);
    long GetParameterStringA(const char* param, char* str);
    long GetParameterStringW(const char* param, unsigned short* wszStr);

    long GetLevel(long nType, long nuChannel, float* value);
    long GetMidiMessage(unsigned char* buffer, long maxBytes);
    long SendMidiMessage(unsigned char* buffer, long numBytes);

    long SetParameterFloat(const char* param, float value);
    long SetParameters(const char* script);
    long SetParametersW(unsigned short* script);
    long SetParameterStringA(const char* param, char* str);
    long SetParameterStringW(const char* param, unsigned short* wszStr);

    long Output_GetDeviceNumber();
    long Output_GetDeviceDescA(long zindex, long* nType, char* szDeviceName, char* szHardwareId);
    long Output_GetDeviceDescW(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId);
    long Input_GetDeviceNumber();
    long Input_GetDeviceDescA(long zindex, long* nType, char* szDeviceName, char* szHardwareId);
    long Input_GetDeviceDescW(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId);

    long AudioCallbackRegister(long mode, T_VBVMR_VBAUDIOCALLBACK pCallback, void* lpUser, char szClientName[64]);
    long AudioCallbackStart();
    long AudioCallbackStop();
    long AudioCallbackUnregister();

    long MacroButton_IsDirty();
    long MacroButton_GetStatus(long nuLogicalButton, float* pValue, long bitmode);
    long MacroButton_SetStatus(long nuLogicalButton, float fValue, long bitmode);

private:
    HMODULE hVoicemeeter = NULL;

    // Function pointers
    T_VBVMR_Login VBVMR_Login = NULL;
    T_VBVMR_Logout VBVMR_Logout = NULL;
    T_VBVMR_RunVoicemeeter VBVMR_RunVoicemeeter = NULL;

    T_VBVMR_GetVoicemeeterType VBVMR_GetVoicemeeterType = NULL;
    T_VBVMR_GetVoicemeeterVersion VBVMR_GetVoicemeeterVersion = NULL;

    T_VBVMR_IsParametersDirty VBVMR_IsParametersDirty = NULL;
    T_VBVMR_GetParameterFloat VBVMR_GetParameterFloat = NULL;
    T_VBVMR_GetParameterStringA VBVMR_GetParameterStringA = NULL;
    T_VBVMR_GetParameterStringW VBVMR_GetParameterStringW = NULL;

    T_VBVMR_GetLevel VBVMR_GetLevel = NULL;
    T_VBVMR_GetMidiMessage VBVMR_GetMidiMessage = NULL;
    T_VBVMR_SendMidiMessage VBVMR_SendMidiMessage = NULL;

    T_VBVMR_SetParameterFloat VBVMR_SetParameterFloat = NULL;
    T_VBVMR_SetParameters VBVMR_SetParameters = NULL;
    T_VBVMR_SetParametersW VBVMR_SetParametersW = NULL;
    T_VBVMR_SetParameterStringA VBVMR_SetParameterStringA = NULL;
    T_VBVMR_SetParameterStringW VBVMR_SetParameterStringW = NULL;

    T_VBVMR_Output_GetDeviceNumber VBVMR_Output_GetDeviceNumber = NULL;
    T_VBVMR_Output_GetDeviceDescA VBVMR_Output_GetDeviceDescA = NULL;
    T_VBVMR_Output_GetDeviceDescW VBVMR_Output_GetDeviceDescW = NULL;
    T_VBVMR_Input_GetDeviceNumber VBVMR_Input_GetDeviceNumber = NULL;
    T_VBVMR_Input_GetDeviceDescA VBVMR_Input_GetDeviceDescA = NULL;
    T_VBVMR_Input_GetDeviceDescW VBVMR_Input_GetDeviceDescW = NULL;

    T_VBVMR_AudioCallbackRegister VBVMR_AudioCallbackRegister = NULL;
    T_VBVMR_AudioCallbackStart VBVMR_AudioCallbackStart = NULL;
    T_VBVMR_AudioCallbackStop VBVMR_AudioCallbackStop = NULL;
    T_VBVMR_AudioCallbackUnregister VBVMR_AudioCallbackUnregister = NULL;

    T_VBVMR_MacroButton_IsDirty VBVMR_MacroButton_IsDirty = NULL;
    T_VBVMR_MacroButton_GetStatus VBVMR_MacroButton_GetStatus = NULL;
    T_VBVMR_MacroButton_SetStatus VBVMR_MacroButton_SetStatus = NULL;

    // Disable copy and assignment
    VoicemeeterAPI(const VoicemeeterAPI&) = delete;
    VoicemeeterAPI& operator=(const VoicemeeterAPI&) = delete;
};

#endif // VOICEMETER_API_H
