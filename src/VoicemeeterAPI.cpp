// src/VoicemeeterAPI.cpp

#include "VoicemeeterAPI.h"
#include <iostream>

// Constructor
VoicemeeterAPI::VoicemeeterAPI() {}

// Destructor
VoicemeeterAPI::~VoicemeeterAPI() {
    Shutdown();
}

bool VoicemeeterAPI::Initialize() {
    // Determine DLL name based on architecture
#ifdef _WIN64
    const char* dllName = "VoicemeeterRemote64.dll";
#else
    const char* dllName = "VoicemeeterRemote.dll";
#endif

    // Load the DLL
    hVoicemeeter = LoadLibraryA(dllName);
    if (!hVoicemeeter) {
        std::cerr << "Failed to load " << dllName << std::endl;
        return false;
    }

    // Retrieve function pointers
    VBVMR_Login = (T_VBVMR_Login)GetProcAddress(hVoicemeeter, "VBVMR_Login");
    VBVMR_Logout = (T_VBVMR_Logout)GetProcAddress(hVoicemeeter, "VBVMR_Logout");
    VBVMR_RunVoicemeeter = (T_VBVMR_RunVoicemeeter)GetProcAddress(hVoicemeeter, "VBVMR_RunVoicemeeter");

    VBVMR_GetVoicemeeterType = (T_VBVMR_GetVoicemeeterType)GetProcAddress(hVoicemeeter, "VBVMR_GetVoicemeeterType");
    VBVMR_GetVoicemeeterVersion = (T_VBVMR_GetVoicemeeterVersion)GetProcAddress(hVoicemeeter, "VBVMR_GetVoicemeeterVersion");

    VBVMR_IsParametersDirty = (T_VBVMR_IsParametersDirty)GetProcAddress(hVoicemeeter, "VBVMR_IsParametersDirty");
    VBVMR_GetParameterFloat = (T_VBVMR_GetParameterFloat)GetProcAddress(hVoicemeeter, "VBVMR_GetParameterFloat");
    VBVMR_GetParameterStringA = (T_VBVMR_GetParameterStringA)GetProcAddress(hVoicemeeter, "VBVMR_GetParameterStringA");
    VBVMR_GetParameterStringW = (T_VBVMR_GetParameterStringW)GetProcAddress(hVoicemeeter, "VBVMR_GetParameterStringW");

    VBVMR_GetLevel = (T_VBVMR_GetLevel)GetProcAddress(hVoicemeeter, "VBVMR_GetLevel");
    VBVMR_GetMidiMessage = (T_VBVMR_GetMidiMessage)GetProcAddress(hVoicemeeter, "VBVMR_GetMidiMessage");
    VBVMR_SendMidiMessage = (T_VBVMR_SendMidiMessage)GetProcAddress(hVoicemeeter, "VBVMR_SendMidiMessage");

    VBVMR_SetParameterFloat = (T_VBVMR_SetParameterFloat)GetProcAddress(hVoicemeeter, "VBVMR_SetParameterFloat");
    VBVMR_SetParameters = (T_VBVMR_SetParameters)GetProcAddress(hVoicemeeter, "VBVMR_SetParameters");
    VBVMR_SetParametersW = (T_VBVMR_SetParametersW)GetProcAddress(hVoicemeeter, "VBVMR_SetParametersW");
    VBVMR_SetParameterStringA = (T_VBVMR_SetParameterStringA)GetProcAddress(hVoicemeeter, "VBVMR_SetParameterStringA");
    VBVMR_SetParameterStringW = (T_VBVMR_SetParameterStringW)GetProcAddress(hVoicemeeter, "VBVMR_SetParameterStringW");

    VBVMR_Output_GetDeviceNumber = (T_VBVMR_Output_GetDeviceNumber)GetProcAddress(hVoicemeeter, "VBVMR_Output_GetDeviceNumber");
    VBVMR_Output_GetDeviceDescA = (T_VBVMR_Output_GetDeviceDescA)GetProcAddress(hVoicemeeter, "VBVMR_Output_GetDeviceDescA");
    VBVMR_Output_GetDeviceDescW = (T_VBVMR_Output_GetDeviceDescW)GetProcAddress(hVoicemeeter, "VBVMR_Output_GetDeviceDescW");
    VBVMR_Input_GetDeviceNumber = (T_VBVMR_Input_GetDeviceNumber)GetProcAddress(hVoicemeeter, "VBVMR_Input_GetDeviceNumber");
    VBVMR_Input_GetDeviceDescA = (T_VBVMR_Input_GetDeviceDescA)GetProcAddress(hVoicemeeter, "VBVMR_Input_GetDeviceDescA");
    VBVMR_Input_GetDeviceDescW = (T_VBVMR_Input_GetDeviceDescW)GetProcAddress(hVoicemeeter, "VBVMR_Input_GetDeviceDescW");

    VBVMR_AudioCallbackRegister = (T_VBVMR_AudioCallbackRegister)GetProcAddress(hVoicemeeter, "VBVMR_AudioCallbackRegister");
    VBVMR_AudioCallbackStart = (T_VBVMR_AudioCallbackStart)GetProcAddress(hVoicemeeter, "VBVMR_AudioCallbackStart");
    VBVMR_AudioCallbackStop = (T_VBVMR_AudioCallbackStop)GetProcAddress(hVoicemeeter, "VBVMR_AudioCallbackStop");
    VBVMR_AudioCallbackUnregister = (T_VBVMR_AudioCallbackUnregister)GetProcAddress(hVoicemeeter, "VBVMR_AudioCallbackUnregister");

    VBVMR_MacroButton_IsDirty = (T_VBVMR_MacroButton_IsDirty)GetProcAddress(hVoicemeeter, "VBVMR_MacroButton_IsDirty");
    VBVMR_MacroButton_GetStatus = (T_VBVMR_MacroButton_GetStatus)GetProcAddress(hVoicemeeter, "VBVMR_MacroButton_GetStatus");
    VBVMR_MacroButton_SetStatus = (T_VBVMR_MacroButton_SetStatus)GetProcAddress(hVoicemeeter, "VBVMR_MacroButton_SetStatus");

    // Verify all required functions are loaded
    if (!VBVMR_Login || !VBVMR_Logout || !VBVMR_RunVoicemeeter ||
        !VBVMR_GetVoicemeeterType || !VBVMR_GetVoicemeeterVersion ||
        !VBVMR_IsParametersDirty || !VBVMR_GetParameterFloat ||
        !VBVMR_GetParameterStringA || !VBVMR_GetParameterStringW ||
        !VBVMR_GetLevel || !VBVMR_GetMidiMessage || !VBVMR_SendMidiMessage ||
        !VBVMR_SetParameterFloat || !VBVMR_SetParameters || !VBVMR_SetParametersW ||
        !VBVMR_SetParameterStringA || !VBVMR_SetParameterStringW ||
        !VBVMR_Output_GetDeviceNumber || !VBVMR_Output_GetDeviceDescA ||
        !VBVMR_Output_GetDeviceDescW || !VBVMR_Input_GetDeviceNumber ||
        !VBVMR_Input_GetDeviceDescA || !VBVMR_Input_GetDeviceDescW ||
        !VBVMR_AudioCallbackRegister || !VBVMR_AudioCallbackStart ||
        !VBVMR_AudioCallbackStop || !VBVMR_AudioCallbackUnregister ||
        !VBVMR_MacroButton_IsDirty || !VBVMR_MacroButton_GetStatus ||
        !VBVMR_MacroButton_SetStatus) {
        std::cerr << "Failed to retrieve one or more Voicemeeter API functions." << std::endl;
        return false;
    }

    return true;
}

void VoicemeeterAPI::Shutdown() {
    if (hVoicemeeter) {
        VBVMR_Logout();
        FreeLibrary(hVoicemeeter);
        hVoicemeeter = NULL;
    }
}

long VoicemeeterAPI::Login() {
    if (VBVMR_Login) {
        return VBVMR_Login();
    }
    return -1;
}

long VoicemeeterAPI::Logout() {
    if (VBVMR_Logout) {
        return VBVMR_Logout();
    }
    return -1;
}

long VoicemeeterAPI::RunVoicemeeter(long vType) {
    if (VBVMR_RunVoicemeeter) {
        return VBVMR_RunVoicemeeter(vType);
    }
    return -1;
}

long VoicemeeterAPI::GetVoicemeeterType(long* pType) {
    if (VBVMR_GetVoicemeeterType) {
        return VBVMR_GetVoicemeeterType(pType);
    }
    return -1;
}

long VoicemeeterAPI::GetVoicemeeterVersion(long* pVersion) {
    if (VBVMR_GetVoicemeeterVersion) {
        return VBVMR_GetVoicemeeterVersion(pVersion);
    }
    return -1;
}

long VoicemeeterAPI::IsParametersDirty() {
    if (VBVMR_IsParametersDirty) {
        return VBVMR_IsParametersDirty();
    }
    return -1;
}

long VoicemeeterAPI::GetParameterFloat(const char* param, float* value) {
    if (VBVMR_GetParameterFloat) {
        return VBVMR_GetParameterFloat(const_cast<char*>(param), value);
    }
    return -1;
}

long VoicemeeterAPI::GetParameterStringA(const char* param, char* str) {
    if (VBVMR_GetParameterStringA) {
        return VBVMR_GetParameterStringA(const_cast<char*>(param), str);
    }
    return -1;
}

long VoicemeeterAPI::GetParameterStringW(const char* param, unsigned short* wszStr) {
    if (VBVMR_GetParameterStringW) {
        return VBVMR_GetParameterStringW(const_cast<char*>(param), wszStr);
    }
    return -1;
}

long VoicemeeterAPI::GetLevel(long nType, long nuChannel, float* value) {
    if (VBVMR_GetLevel) {
        return VBVMR_GetLevel(nType, nuChannel, value);
    }
    return -1;
}

long VoicemeeterAPI::GetMidiMessage(unsigned char* buffer, long maxBytes) {
    if (VBVMR_GetMidiMessage) {
        return VBVMR_GetMidiMessage(buffer, maxBytes);
    }
    return -1;
}

long VoicemeeterAPI::SendMidiMessage(unsigned char* buffer, long numBytes) {
    if (VBVMR_SendMidiMessage) {
        return VBVMR_SendMidiMessage(buffer, numBytes);
    }
    return -1;
}

long VoicemeeterAPI::SetParameterFloat(const char* param, float value) {
    if (VBVMR_SetParameterFloat) {
        return VBVMR_SetParameterFloat(const_cast<char*>(param), value);
    }
    return -1;
}

long VoicemeeterAPI::SetParameters(const char* script) {
    if (VBVMR_SetParameters) {
        return VBVMR_SetParameters(const_cast<char*>(script));
    }
    return -1;
}

long VoicemeeterAPI::SetParametersW(unsigned short* script) {
    if (VBVMR_SetParametersW) {
        return VBVMR_SetParametersW(script);
    }
    return -1;
}

long VoicemeeterAPI::SetParameterStringA(const char* param, char* str) {
    if (VBVMR_SetParameterStringA) {
        return VBVMR_SetParameterStringA(const_cast<char*>(param), str);
    }
    return -1;
}

long VoicemeeterAPI::SetParameterStringW(const char* param, unsigned short* wszStr) {
    if (VBVMR_SetParameterStringW) {
        return VBVMR_SetParameterStringW(const_cast<char*>(param), wszStr);
    }
    return -1;
}

long VoicemeeterAPI::Output_GetDeviceNumber() {
    if (VBVMR_Output_GetDeviceNumber) {
        return VBVMR_Output_GetDeviceNumber();
    }
    return -1;
}

long VoicemeeterAPI::Output_GetDeviceDescA(long zindex, long* nType, char* szDeviceName, char* szHardwareId) {
    if (VBVMR_Output_GetDeviceDescA) {
        return VBVMR_Output_GetDeviceDescA(zindex, nType, szDeviceName, szHardwareId);
    }
    return -1;
}

long VoicemeeterAPI::Output_GetDeviceDescW(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId) {
    if (VBVMR_Output_GetDeviceDescW) {
        return VBVMR_Output_GetDeviceDescW(zindex, nType, wszDeviceName, wszHardwareId);
    }
    return -1;
}

long VoicemeeterAPI::Input_GetDeviceNumber() {
    if (VBVMR_Input_GetDeviceNumber) {
        return VBVMR_Input_GetDeviceNumber();
    }
    return -1;
}

long VoicemeeterAPI::Input_GetDeviceDescA(long zindex, long* nType, char* szDeviceName, char* szHardwareId) {
    if (VBVMR_Input_GetDeviceDescA) {
        return VBVMR_Input_GetDeviceDescA(zindex, nType, szDeviceName, szHardwareId);
    }
    return -1;
}

long VoicemeeterAPI::Input_GetDeviceDescW(long zindex, long* nType, unsigned short* wszDeviceName, unsigned short* wszHardwareId) {
    if (VBVMR_Input_GetDeviceDescW) {
        return VBVMR_Input_GetDeviceDescW(zindex, nType, wszDeviceName, wszHardwareId);
    }
    return -1;
}

long VoicemeeterAPI::AudioCallbackRegister(long mode, T_VBVMR_VBAUDIOCALLBACK pCallback, void* lpUser, char szClientName[64]) {
    if (VBVMR_AudioCallbackRegister) {
        return VBVMR_AudioCallbackRegister(mode, pCallback, lpUser, szClientName);
    }
    return -1;
}

long VoicemeeterAPI::AudioCallbackStart() {
    if (VBVMR_AudioCallbackStart) {
        return VBVMR_AudioCallbackStart();
    }
    return -1;
}

long VoicemeeterAPI::AudioCallbackStop() {
    if (VBVMR_AudioCallbackStop) {
        return VBVMR_AudioCallbackStop();
    }
    return -1;
}

long VoicemeeterAPI::AudioCallbackUnregister() {
    if (VBVMR_AudioCallbackUnregister) {
        return VBVMR_AudioCallbackUnregister();
    }
    return -1;
}

long VoicemeeterAPI::MacroButton_IsDirty() {
    if (VBVMR_MacroButton_IsDirty) {
        return VBVMR_MacroButton_IsDirty();
    }
    return -1;
}

long VoicemeeterAPI::MacroButton_GetStatus(long nuLogicalButton, float* pValue, long bitmode) {
    if (VBVMR_MacroButton_GetStatus) {
        return VBVMR_MacroButton_GetStatus(nuLogicalButton, pValue, bitmode);
    }
    return -1;
}

long VoicemeeterAPI::MacroButton_SetStatus(long nuLogicalButton, float fValue, long bitmode) {
    if (VBVMR_MacroButton_SetStatus) {
        return VBVMR_MacroButton_SetStatus(nuLogicalButton, fValue, bitmode);
    }
    return -1;
}
