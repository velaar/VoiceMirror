#include "VoicemeeterManager.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "Logger.h"
#include "VolumeUtils.h"

#define DEFAULT_DLL_PATH_64 "C:\\Program Files\\VB\\Voicemeeter\\VoicemeeterRemote64.dll"
#define DEFAULT_DLL_PATH_32 "C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote.dll"
#define MAX_RETRIES 10
#define RETRY_DELAY_MS 500

VoicemeeterManager::VoicemeeterManager()
    : VBVMR_Login(nullptr),
      VBVMR_Logout(nullptr),
      VBVMR_RunVoicemeeter(nullptr),
      VBVMR_GetVoicemeeterType(nullptr),
      VBVMR_GetVoicemeeterVersion(nullptr),
      VBVMR_IsParametersDirty(nullptr),
      VBVMR_GetParameterFloat(nullptr),
      VBVMR_GetParameterStringA(nullptr),
      VBVMR_GetParameterStringW(nullptr),
      VBVMR_SetParameterFloat(nullptr),
      initialized(false),
      loggedIn(false),
      debugMode(false) {
}

VoicemeeterManager::~VoicemeeterManager() {
    Shutdown();
}

bool VoicemeeterManager::LoadVoicemeeterRemote() {
    if (initialized) {
        LOG_DEBUG("VoicemeeterRemote DLL is already loaded.");
        return true;
    }

#ifdef _WIN64
    const char* dllFullPath = DEFAULT_DLL_PATH_64;
#else
    const char* dllFullPath = DEFAULT_DLL_PATH_32;
#endif

    LOG_DEBUG("Loading VoicemeeterRemote DLL from: " + std::string(dllFullPath));

    // Load the DLL
    HMODULE hModule = LoadLibraryA(dllFullPath);
    if (!hModule) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to load VoicemeeterRemote DLL. Error code: " + std::to_string(error));
        return false;
    }

    // Assign the loaded module to RAIIHMODULE
    hVoicemeeterRemote = RAIIHMODULE(hModule);

    // Retrieve function pointers with proper casting
    VBVMR_Login = reinterpret_cast<T_VBVMR_Login>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_Login"));
    VBVMR_Logout = reinterpret_cast<T_VBVMR_Logout>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_Logout"));
    VBVMR_RunVoicemeeter = reinterpret_cast<T_VBVMR_RunVoicemeeter>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_RunVoicemeeter"));
    VBVMR_GetVoicemeeterType = reinterpret_cast<T_VBVMR_GetVoicemeeterType>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_GetVoicemeeterType"));
    VBVMR_GetVoicemeeterVersion = reinterpret_cast<T_VBVMR_GetVoicemeeterVersion>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_GetVoicemeeterVersion"));
    VBVMR_IsParametersDirty = reinterpret_cast<T_VBVMR_IsParametersDirty>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_IsParametersDirty"));
    VBVMR_GetParameterFloat = reinterpret_cast<T_VBVMR_GetParameterFloat>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_GetParameterFloat"));
    VBVMR_GetParameterStringA = reinterpret_cast<T_VBVMR_GetParameterStringA>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_GetParameterStringA"));
    VBVMR_GetParameterStringW = reinterpret_cast<T_VBVMR_GetParameterStringW>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_GetParameterStringW"));
    VBVMR_SetParameterFloat = reinterpret_cast<T_VBVMR_SetParameterFloat>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_SetParameterFloat"));

    // Verify all required function pointers are loaded
    if (!VBVMR_Login || !VBVMR_Logout || !VBVMR_RunVoicemeeter ||
        !VBVMR_GetVoicemeeterType || !VBVMR_GetVoicemeeterVersion ||
        !VBVMR_IsParametersDirty || !VBVMR_GetParameterFloat ||
        !VBVMR_GetParameterStringA || !VBVMR_GetParameterStringW ||
        !VBVMR_SetParameterFloat) {
        LOG_ERROR("Failed to retrieve all required function pointers from VoicemeeterRemote DLL.");
        UnloadVoicemeeterRemote();
        return false;
    }

    LOG_DEBUG("VoicemeeterRemote DLL loaded and all function pointers retrieved successfully.");
    initialized = true;
    return true;
}

// Unload Voicemeeter Remote DLL and reset function pointers
void VoicemeeterManager::UnloadVoicemeeterRemote() {
    if (hVoicemeeterRemote.get()) {
        hVoicemeeterRemote = RAIIHMODULE(); // Reset to default, which unloads the DLL
        LOG_DEBUG("VoicemeeterRemote DLL unloaded.");
    }

    // Reset function pointers
    VBVMR_Login = nullptr;
    VBVMR_Logout = nullptr;
    VBVMR_RunVoicemeeter = nullptr;
    VBVMR_GetVoicemeeterType = nullptr;
    VBVMR_GetVoicemeeterVersion = nullptr;
    VBVMR_IsParametersDirty = nullptr;
    VBVMR_GetParameterFloat = nullptr;
    VBVMR_GetParameterStringA = nullptr;
    VBVMR_GetParameterStringW = nullptr;
    VBVMR_SetParameterFloat = nullptr;

    initialized = false;
}

// Initialize Voicemeeter Manager
bool VoicemeeterManager::Initialize(int voicemeeterType) {
    if (loggedIn) {
        LOG_DEBUG("Voicemeeter is already logged in.");
        return true;
    }

    LOG_DEBUG("Initializing Voicemeeter Manager...");

    if (!LoadVoicemeeterRemote()) {
        LOG_ERROR("Failed to load VoicemeeterRemote DLL.");
        return false;
    }

    long loginResult = VBVMR_Login();
    LOG_DEBUG("Voicemeeter login result: " + std::to_string(loginResult));
    loggedIn = (loginResult == 0 || loginResult == 1);

    long vmType = 0;
    if (loggedIn) {
        HRESULT hr = VBVMR_GetVoicemeeterType(&vmType);
        LOG_DEBUG("GetVoicemeeterType result: " + std::to_string(hr) +
                  ", Type: " + std::to_string(vmType));
        if (hr != 0) {
            LOG_WARNING("Voicemeeter is not running. Attempting to start it.");
            loggedIn = false;
        }
    }

    if (!loggedIn) {
        LOG_WARNING("Voicemeeter login failed, attempting to run Voicemeeter Type: " +
                    std::to_string(voicemeeterType));
        long runResult = VBVMR_RunVoicemeeter(voicemeeterType);
        LOG_DEBUG("RunVoicemeeter result: " + std::to_string(runResult));

        if (runResult != 0) {
            LOG_ERROR("Failed to run Voicemeeter. Error code: " +
                      std::to_string(runResult));
            UnloadVoicemeeterRemote();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        loginResult = VBVMR_Login();
        LOG_DEBUG("Voicemeeter login result after running: " +
                  std::to_string(loginResult));
        loggedIn = (loginResult == -2 || loginResult == 0);

        if (loggedIn) {
            HRESULT hr = VBVMR_GetVoicemeeterType(&vmType);
            LOG_DEBUG("GetVoicemeeterType result after running: " + std::to_string(hr) +
                      ", Type: " + std::to_string(vmType));
            if (hr != 0) {
                LOG_ERROR("Failed to start Voicemeeter.");
                loggedIn = false;
            }
        }
    }

    if (loggedIn) {
        LOG_DEBUG("Starting check for Voicemeeter audio engine status.");

        bool audioEngineRunning = false;

        for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            long isDirty = VBVMR_IsParametersDirty();

            if (isDirty == 1) {
                audioEngineRunning = true;
                LOG_DEBUG("Audio engine running on attempt " + std::to_string(attempt + 1) +
                          ". Parameters are clean, indicating engine is ready.");
                break;
            } else if (isDirty == -1 || isDirty == -2) {
                LOG_WARNING("Attempt " + std::to_string(attempt + 1) +
                            ": Voicemeeter not properly initialized.");
            } else if (isDirty == 0) {
                LOG_DEBUG("Attempt " + std::to_string(attempt + 1) +
                          ": Parameters are dirty, engine may still be starting.");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }

        if (!audioEngineRunning) {
            LOG_ERROR("Voicemeeter audio engine check failed after " +
                      std::to_string(MAX_RETRIES) + " attempts. Engine not responsive.");
            Shutdown();
            return false;
        }

        LOG_DEBUG("Voicemeeter login and audio engine confirmation successful.");

        std::string typeStr;
        int maxStrips = 0;
        int maxBuses = 0;

        switch (vmType) {
            case 1:
                typeStr = "Voicemeeter";
                maxStrips = 3;
                maxBuses = 2;
                break;
            case 2:
                typeStr = "Voicemeeter Banana";
                maxStrips = 5;
                maxBuses = 5;
                break;
            case 3:
                typeStr = "Voicemeeter Potato";
                maxStrips = 8;
                maxBuses = 8;
                break;
            default:
                LOG_ERROR("Unknown Voicemeeter type.");
                Shutdown();
                return false;
        }

        LOG_INFO("Voicemeeter Type: " + typeStr);
    } else {
        LOG_ERROR("Voicemeeter login failed.");
        UnloadVoicemeeterRemote();
    }

    return loggedIn;
}

// Shutdown Voicemeeter Manager
void VoicemeeterManager::Shutdown() {
    if (loggedIn) {
        LOG_DEBUG("Shutting down Voicemeeter.");
        if (VBVMR_Logout) {
            VBVMR_Logout();
        }
        UnloadVoicemeeterRemote();
        loggedIn = false;
    }
}


// Send Shutdown Command to Voicemeeter
void VoicemeeterManager::ShutdownCommand() {
    if (VBVMR_SetParameterFloat) {
        LOG_DEBUG("Sending shutdown command to Voicemeeter.");
        if (VBVMR_SetParameterFloat(const_cast<char*>("Command.Shutdown"), 1.0f) != 0) {
            LOG_ERROR("Failed to send shutdown command.");
        }
    } else {
        LOG_ERROR("VBVMR_SetParameterFloat is not available.");
    }
}

// Restart Voicemeeter Audio Engine
void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    LOG_DEBUG("Restarting Voicemeeter audio engine...");

    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    if (VBVMR_SetParameterFloat) {
        VBVMR_SetParameterFloat("Command.Restart", 1.0f);
    } else {
        LOG_ERROR("VBVMR_SetParameterFloat is not available.");
    }

    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));

    LOG_DEBUG("Voicemeeter audio engine restarted.");
}

// Set Debug Mode
void VoicemeeterManager::SetDebugMode(bool newDebugMode) {
    this->debugMode = newDebugMode;
}

// Get Debug Mode
bool VoicemeeterManager::GetDebugMode() {
    return debugMode;
}

// List All Channels
void VoicemeeterManager::ListAllChannels() {

    long voicemeeterType = 0;
    if (VBVMR_GetVoicemeeterType) {
        HRESULT hr = VBVMR_GetVoicemeeterType(&voicemeeterType);
        if (hr != 0) {
            LOG_ERROR("Failed to get Voicemeeter type.");
            return;
        }
    } else {
        LOG_ERROR("VBVMR_GetVoicemeeterType is not available.");
        return;
    }

    std::string typeStr;
    int maxStrips = 0;
    int maxBuses = 0;

    switch (voicemeeterType) {
        case 1:
            typeStr = "Voicemeeter";
            maxStrips = 3;
            maxBuses = 2;
            break;
        case 2:
            typeStr = "Voicemeeter Banana";
            maxStrips = 5;
            maxBuses = 5;
            break;
        case 3:
            typeStr = "Voicemeeter Potato";
            maxStrips = 8;
            maxBuses = 8;
            break;
        default:
            LOG_ERROR("Unknown Voicemeeter type.");
            return;
    }

    LOG_INFO("Voicemeeter Type: " + typeStr);

    auto PrintParameter = [&](const std::string& param, const std::string& type, int index) {
        char label[512] = {0};
        if (VBVMR_GetParameterStringA) {
            long result = VBVMR_GetParameterStringA(const_cast<char*>(param.c_str()), label);
            if (result == 0 && strlen(label) > 0 && strlen(label) < sizeof(label)) {
                LOG_INFO("| " + std::to_string(index) +
                         " | " + label + " | " + type +
                         " |");
            } else {
                LOG_INFO("| " + std::to_string(index) +
                         " | N/A | " + type + " |");
            }
        } else {
            LOG_ERROR("VBVMR_GetParameterStringA is not available.");
            LOG_INFO("| " + std::to_string(index) +
                     " | N/A | " + type + " |");
        }
    };

    LOG_INFO("\nStrips:");
    LOG_INFO("+---------+----------------------+--------------+");
    LOG_INFO("| Index   | Label                | Type         |");
    LOG_INFO("+---------+----------------------+--------------+");

    for (int i = 0; i < maxStrips; ++i) {
        std::string paramName = "Strip[" + std::to_string(i) + "].Label";
        PrintParameter(paramName, "Input Strip", i);
    }
    LOG_INFO("+---------+----------------------+--------------+");

    LOG_INFO("\nBuses:");
    LOG_INFO("+---------+----------------------+--------------+");
    LOG_INFO("| Index   | Label                | Type         |");
    LOG_INFO("+---------+----------------------+--------------+");

    for (int i = 0; i < maxBuses; ++i) {
        std::string paramNameBus = "Bus[" + std::to_string(i) + "].Label";
        std::string busType = "BUS " + std::to_string(i);
        PrintParameter(paramNameBus, busType, i);
    }
    LOG_INFO("+---------+----------------------+--------------+");
}

// List Inputs
void VoicemeeterManager::ListInputs() {

    try {
        int maxStrips = 8;
        LOG_INFO("Available Voicemeeter Virtual Inputs:");

        for (int i = 0; i < maxStrips; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512] = {0};
            if (VBVMR_GetParameterStringA) {
                long result = VBVMR_GetParameterStringA(paramName, label);
                if (result == 0 && strlen(label) > 0) {
                    LOG_INFO(std::to_string(i) + ": " + label);
                } else {
                    break;
                }
            } else {
                LOG_ERROR("VBVMR_GetParameterStringA is not available.");
                break;
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Error listing Voicemeeter inputs: " + std::string(ex.what()));
    }
}

// List Outputs
void VoicemeeterManager::ListOutputs() {

    try {
        int maxBuses = 8;
        LOG_INFO("Available Voicemeeter Virtual Outputs:");

        for (int i = 0; i < maxBuses; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256] = {0};
            if (VBVMR_GetParameterStringA) {
                long result = VBVMR_GetParameterStringA(paramName, label);
                if (result == 0 && strlen(label) > 0) {
                    LOG_INFO(std::to_string(i) + ": " + label);
                } else {
                    break;
                }
            } else {
                LOG_ERROR("VBVMR_GetParameterStringA is not available.");
                break;
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Error listing Voicemeeter outputs: " + std::string(ex.what()));
    }
}

// Get Voicemeeter Volume and Mute State
bool VoicemeeterManager::GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted) {
    std::lock_guard<std::mutex> lock(channelMutex);

    float gainValue = 0.0f;
    float muteValue = 0.0f;
    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (VBVMR_GetParameterFloat &&
        VBVMR_GetParameterFloat(const_cast<char*>(gainParam.c_str()), &gainValue) == 0) {
        volumePercent = VolumeUtils::dBmToPercent(gainValue);
    } else {
        LOG_DEBUG("Failed to get Gain parameter for " + gainParam);
        return false;
    }

    if (VBVMR_GetParameterFloat &&
        VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue) == 0) {
        isMuted = (muteValue != 0.0f);
    } else {
        LOG_DEBUG("Failed to get Mute parameter for " + muteParam);
        return false;
    }

    LOG_DEBUG("Volume: " + std::to_string(volumePercent) + "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
    return true;
}

// Update Voicemeeter Volume and Mute State
void VoicemeeterManager::UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted) {
    std::lock_guard<std::mutex> lock(channelMutex);

    if (!VBVMR_SetParameterFloat) {
        LOG_ERROR("VBVMR_SetParameterFloat is not available.");
        return;
    }

    float dBmValue = VolumeUtils::PercentToDbm(volumePercent);

    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (VBVMR_SetParameterFloat(const_cast<char*>(gainParam.c_str()), dBmValue) != 0) {
        LOG_ERROR("Failed to set Gain parameter for " + gainParam);
    }

    if (VBVMR_SetParameterFloat(const_cast<char*>(muteParam.c_str()), isMuted ? 1.0f : 0.0f) != 0) {
        LOG_ERROR("Failed to set Mute parameter for " + muteParam);
    }

    LOG_DEBUG("Voicemeeter volume updated: " + std::to_string(volumePercent) + "% " +
              (isMuted ? "(Muted)" : "(Unmuted)"));
}

// Check if Parameters are Dirty
bool VoicemeeterManager::IsParametersDirty() {

    if (!loggedIn) {
        LOG_ERROR("Cannot check parameters dirty state: not logged in.");
        return false;
    }

    if (!VBVMR_IsParametersDirty) {
        LOG_ERROR("VBVMR_IsParametersDirty is not available.");
        return false;
    }

    long result = VBVMR_IsParametersDirty();
    switch (result) {
        case 0:
            LOG_DEBUG("IsParametersDirty: Parameters have not changed (not dirty).");
            return false;
        case 1:
            LOG_DEBUG("IsParametersDirty: Parameters have changed (dirty).");
            return true;
        case -1:
            LOG_ERROR("IsParametersDirty: Unexpected error occurred.");
            return false;
        case -2:
            LOG_ERROR("IsParametersDirty: No Voicemeeter server detected.");
            return false;
        default:
            LOG_ERROR("IsParametersDirty: Unknown result code: " + std::to_string(result));
            return false;
    }
}

// Get Channel Volume
bool VoicemeeterManager::GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent) {
    std::lock_guard<std::mutex> lock(channelMutex);

    float gainValue = 0.0f;
    std::string gainParam;

    if (channelType == ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
    } else {
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
    }

    if (VBVMR_GetParameterFloat &&
        VBVMR_GetParameterFloat(const_cast<char*>(gainParam.c_str()), &gainValue) == 0) {
        volumePercent = VolumeUtils::dBmToPercent(gainValue);
        LOG_DEBUG("Channel " + std::to_string(channelIndex) + " Volume: " + std::to_string(volumePercent) + "%");
        return true;
    } else {
        LOG_DEBUG("Failed to get Gain parameter for " + gainParam);
        return false;
    }
}

// Check if Channel is Muted
bool VoicemeeterManager::IsChannelMuted(int channelIndex, ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelMutex);

    float muteValue = 0.0f;
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (VBVMR_GetParameterFloat &&
        VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue) == 0) {
        bool isMuted = (muteValue != 0.0f);
        LOG_DEBUG("Channel " + std::to_string(channelIndex) + " Mute State: " + (isMuted ? "Muted" : "Unmuted"));
        return isMuted;
    } else {
        LOG_DEBUG("Failed to get Mute parameter for " + muteParam);
        return false;
    }
}

// Set Mute State
bool VoicemeeterManager::SetMute(int channelIndex, ChannelType channelType, bool isMuted) {
    std::lock_guard<std::mutex> lock(channelMutex);
    return SetMuteInternal(channelIndex, channelType, isMuted);
}

// Internal Mute Handling
bool VoicemeeterManager::SetMuteInternal(int channelIndex, ChannelType channelType, bool isMuted) {
    if (!VBVMR_SetParameterFloat) {
        LOG_ERROR("VBVMR_SetParameterFloat is not available.");
        return false;
    }

    std::string muteParam;

    if (channelType == ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    float muteValue = isMuted ? 1.0f : 0.0f;
    long result = VBVMR_SetParameterFloat(const_cast<char*>(muteParam.c_str()), muteValue);

    if (result != 0) {
        LOG_ERROR("Failed to set Mute parameter for " + muteParam + ". Error code: " + std::to_string(result));
        return false;
    }

    LOG_DEBUG("Channel " + std::to_string(channelIndex) + " mute state set to " + (isMuted ? "Muted" : "Unmuted"));
    return true;
}
