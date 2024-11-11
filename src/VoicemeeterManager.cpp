// VoicemeeterManager.cpp
#include "VoicemeeterManager.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "Defconf.h"
#include "Logger.h"
#include "RAIIHandle.h"
#include "VolumeUtils.h"

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
      loggedIn(false) {
    LOG_DEBUG("[VoicemeeterManager::VoicemeeterManager] Constructor called.");
}

VoicemeeterManager::~VoicemeeterManager() {
    LOG_DEBUG("[VoicemeeterManager::~VoicemeeterManager] Destructor called.");
    Shutdown();
}

bool VoicemeeterManager::Initialize(int voicemeeterType) {
    LOG_DEBUG("[VoicemeeterManager::Initialize] Initialization started.");

    if (!LoadVoicemeeterRemote()) {
        LOG_ERROR("[VoicemeeterManager::Initialize] Failed to load VoicemeeterRemote DLL.");
        return false;
    }

    if (!loggedIn) {
        long loginResult = VBVMR_Login();
        LOG_DEBUG("[VoicemeeterManager::Initialize] Voicemeeter login result: " + std::to_string(loginResult));
        if (loginResult != 0 && loginResult != -2) {  // -2 might indicate already logged in
            LOG_WARNING("[VoicemeeterManager::Initialize] Voicemeeter login failed, attempting to run Voicemeeter Type: " + std::to_string(voicemeeterType));
            long runResult = VBVMR_RunVoicemeeter(voicemeeterType);
            std::this_thread::sleep_for(std::chrono::milliseconds(DEFAULT_STARTUP_DELAY_MS));

            LOG_DEBUG("[VoicemeeterManager::Initialize] RunVoicemeeter result: " + std::to_string(runResult));
            if (runResult != 0) {
                LOG_ERROR("[VoicemeeterManager::Initialize] Failed to run Voicemeeter. Error code: " + std::to_string(runResult));
                UnloadVoicemeeterRemote();
                return false;
            }
            LOG_DEBUG("[VoicemeeterManager::Initialize] Waiting for Voicemeeter to start...");
            loginResult = VBVMR_Login();
            LOG_DEBUG("[VoicemeeterManager::Initialize] Voicemeeter login result after running: " + std::to_string(loginResult));
            if (loginResult == 0 || loginResult == -2) {
                loggedIn = true;
            } else {
                LOG_ERROR("[VoicemeeterManager::Initialize] Voicemeeter login failed after running Voicemeeter.");
                UnloadVoicemeeterRemote();
                return false;
            }
        } else {
            loggedIn = true;
        }
    }

    // Wait until parameters are not dirty
    bool parametersReady = false;
    for (int attempt = 0; attempt < MAX_RETRIES && !parametersReady; ++attempt) {
        long dirty = VBVMR_IsParametersDirty();
        if (dirty == 1) {
            parametersReady = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }
    }

    if (!parametersReady) {
        LOG_ERROR("[VoicemeeterManager::Initialize] Parameters not ready after retries.");
        Shutdown();
        return false;
    }

    // Get Bus[0].Device.name and Bus[0].Device.sr
    char deviceName[512] = {0};
    float deviceSR = 0.0f;
    VBVMR_GetParameterStringA("Bus[0].Device.name", deviceName);
    VBVMR_GetParameterFloat("Bus[0].Device.sr", &deviceSR);
    LOG_INFO("[VoicemeeterManager::Initialize] A1 Device Name: " + std::string(deviceName));
    LOG_INFO("[VoicemeeterManager::Initialize] A1 Device Sample Rate: " + std::to_string(deviceSR));

    if (deviceSR == 0.0f || deviceName[0] == '\0') {
        if (deviceSR == 0.0f) {
            LOG_WARNING("[VoicemeeterManager::Initialize] A1 Device sample rate is " + std::to_string(deviceSR) + ". Assuming device is defunct.");
        }
        if (deviceName[0] == '\0') {
            LOG_WARNING("[VoicemeeterManager::Initialize] A1 Device name is empty. Assuming device is defunct.");
        }
        // Get first WDM device
        std::string wdmDevice = GetFirstWdmDeviceName();
        if (wdmDevice.empty()) {
            LOG_ERROR("[VoicemeeterManager::Initialize] No WDM devices found to set as A1.");
            Shutdown();
            return false;
        }
        // Set A1 device
        if (!SetA1Device(wdmDevice)) {
            LOG_ERROR("[VoicemeeterManager::Initialize] Failed to set A1 Device to WDM device.");
            Shutdown();
            return false;
        }

        LOG_INFO("[VoicemeeterManager::Initialize] A1 Device after setting WDM: " + std::string(wdmDevice));
    }

    LOG_DEBUG("[VoicemeeterManager::Initialize] Initialization completed successfully.");
    return true;
}

bool VoicemeeterManager::LoadVoicemeeterRemote() {
    LOG_DEBUG("[VoicemeeterManager::LoadVoicemeeterRemote] Loading VoicemeeterRemote DLL.");

    if (initialized) {
        LOG_DEBUG("[VoicemeeterManager::LoadVoicemeeterRemote] VoicemeeterRemote DLL already loaded.");
        return true;
    }

#ifdef _WIN64
    const char* dllFullPath = DEFAULT_DLL_PATH_64;
#else
    const char* dllFullPath = DEFAULT_DLL_PATH_32;
#endif

    LOG_DEBUG("[VoicemeeterManager::LoadVoicemeeterRemote] Loading from: " + std::string(dllFullPath));

    HMODULE hModule = LoadLibraryA(dllFullPath);
    if (!hModule) {
        LOG_ERROR("[VoicemeeterManager::LoadVoicemeeterRemote] LoadLibraryA failed.");
        return false;
    }
    hVoicemeeterRemote = RAIIHMODULE(hModule);

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
    VBVMR_SetParameterStringA = reinterpret_cast<T_VBVMR_SetParameterStringA>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_SetParameterStringA"));
    VBVMR_SetParameters = reinterpret_cast<T_VBVMR_SetParameters>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_SetParameters"));
    VBVMR_Output_GetDeviceNumber = reinterpret_cast<T_VBVMR_Output_GetDeviceNumber>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_Output_GetDeviceNumber"));
    VBVMR_Output_GetDeviceDescA = reinterpret_cast<T_VBVMR_Output_GetDeviceDescA>(GetProcAddress(hVoicemeeterRemote.get(), "VBVMR_Output_GetDeviceDescA"));

    if (!VBVMR_Login || !VBVMR_Logout || !VBVMR_RunVoicemeeter ||
        !VBVMR_GetVoicemeeterType || !VBVMR_GetVoicemeeterVersion ||
        !VBVMR_IsParametersDirty || !VBVMR_GetParameterFloat ||
        !VBVMR_GetParameterStringA || !VBVMR_GetParameterStringW ||
        !VBVMR_SetParameterFloat || !VBVMR_SetParameterStringA ||
        !VBVMR_SetParameters || !VBVMR_Output_GetDeviceNumber ||
        !VBVMR_Output_GetDeviceDescA) {
        LOG_ERROR("[VoicemeeterManager::LoadVoicemeeterRemote] Function pointers retrieval failed.");
        UnloadVoicemeeterRemote();
        return false;
    }

    initialized = true;
    LOG_DEBUG("[VoicemeeterManager::LoadVoicemeeterRemote] VoicemeeterRemote DLL loaded successfully.");
    return true;
}

void VoicemeeterManager::UnloadVoicemeeterRemote() {
    hVoicemeeterRemote = RAIIHMODULE();
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
    VBVMR_SetParameterStringA = nullptr;
    VBVMR_SetParameters = nullptr;
    VBVMR_Output_GetDeviceNumber = nullptr;
    VBVMR_Output_GetDeviceDescA = nullptr;
    initialized = false;
    LOG_DEBUG("[VoicemeeterManager::UnloadVoicemeeterRemote] Unloaded VoicemeeterRemote DLL.");
}

std::string VoicemeeterManager::GetFirstWdmDeviceName() {
    if (!VBVMR_Output_GetDeviceNumber || !VBVMR_Output_GetDeviceDescA) return "";

    long count = VBVMR_Output_GetDeviceNumber();
    int type = 0;
    char name[256] = {0};
    char hwId[256] = {0};

    for (int i = 0; i < count; ++i) {
        if (VBVMR_Output_GetDeviceDescA(i, &type, name, hwId) == 0 && type == 3) {
            name[sizeof(name) - 1] = '\0';
            std::string deviceName(name);
            const std::string prefix = "WDM: ";
            if (deviceName.find(prefix) == 0) {
                deviceName = deviceName.substr(prefix.size());
            }
            LOG_DEBUG("[VoicemeeterManager::GetFirstWdmDeviceName] Found WDM device: " + deviceName);
            return deviceName;
        }
    }

    return "";
}

bool VoicemeeterManager::SetA1Device(const std::string& deviceName) {
    if (!VBVMR_SetParameterStringA) return false;

    if (VBVMR_SetParameterStringA("Bus[0].Device.wdm", deviceName.c_str()) != 0) {
        LOG_ERROR("[VoicemeeterManager::SetA1Device] Failed to set A1 Device to: " + deviceName);
        return false;
    }

    LOG_INFO("[VoicemeeterManager::SetA1Device] A1 Device set to WDM: " + deviceName);

    return true;
}

void VoicemeeterManager::Shutdown() {
    LOG_DEBUG("[VoicemeeterManager::Shutdown] Shutdown initiated.");

    if (loggedIn) {
        if (VBVMR_Logout) {
            VBVMR_Logout();
            LOG_DEBUG("[VoicemeeterManager::Shutdown] Logged out successfully.");
        }
        UnloadVoicemeeterRemote();
        loggedIn = false;
    }

    LOG_DEBUG("[VoicemeeterManager::Shutdown] Shutdown completed.");
}

void VoicemeeterManager::ShutdownCommand() {
    LOG_DEBUG("[VoicemeeterManager::ShutdownCommand] Sending shutdown command.");
    if (VBVMR_SetParameterFloat) {
        if (VBVMR_SetParameterFloat("Command.Shutdown", 1.0f) != 0) {
            LOG_ERROR("[VoicemeeterManager::ShutdownCommand] Failed to send shutdown command.");
        } else {
            LOG_DEBUG("[VoicemeeterManager::ShutdownCommand] Shutdown command sent successfully.");
        }
    } else {
        LOG_ERROR("[VoicemeeterManager::ShutdownCommand] VBVMR_SetParameterFloat is not available.");
    }
}

void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay) {
    LOG_DEBUG("[VoicemeeterManager::RestartAudioEngine] Restarting audio engine.");
    std::lock_guard<std::mutex> lock(toggleMutex);
    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    if (VBVMR_SetParameterFloat) {
        VBVMR_SetParameterFloat("Command.Restart", 1.0f);
        LOG_DEBUG("[VoicemeeterManager::RestartAudioEngine] Restart command sent.");
    } else {
        LOG_ERROR("[VoicemeeterManager::RestartAudioEngine] VBVMR_SetParameterFloat is not available.");
    }
    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));
    LOG_DEBUG("[VoicemeeterManager::RestartAudioEngine] Audio engine restarted.");
}

void VoicemeeterManager::ListAllChannels() {
    LOG_DEBUG("[VoicemeeterManager::ListAllChannels] Listing all channels.");

    long voicemeeterType = 0;
    if (VBVMR_GetVoicemeeterType) {
        HRESULT hr = VBVMR_GetVoicemeeterType(&voicemeeterType);
        if (hr != 0) {
            LOG_ERROR("[VoicemeeterManager::ListAllChannels] Failed to get Voicemeeter type.");
            return;
        }
        LOG_DEBUG("[VoicemeeterManager::ListAllChannels] Voicemeeter type retrieved: " + std::to_string(voicemeeterType));
    } else {
        LOG_ERROR("[VoicemeeterManager::ListAllChannels] VBVMR_GetVoicemeeterType is not available.");
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
        case 4:
            typeStr = "Voicemeeter x64";
            maxStrips = 3;
            maxBuses = 2;
            break;
        case 5:
            typeStr = "Voicemeeter Banana x64";
            maxStrips = 5;
            maxBuses = 5;
            break;
        case 6:
            typeStr = "Voicemeeter Potato x64";
            maxStrips = 8;
            maxBuses = 8;
            break;
        default:
            LOG_ERROR("[VoicemeeterManager::ListAllChannels] Unknown Voicemeeter type.");
            return;
    }

    LOG_INFO("[VoicemeeterManager::ListAllChannels] Voicemeeter Type: " + typeStr);

    auto PrintParameter = [&](const std::string& param, const std::string& type, int index) {
        char label[512] = {0};
        if (VBVMR_GetParameterStringA) {
            long result = VBVMR_GetParameterStringA(const_cast<char*>(param.c_str()), label);
            if (result == 0 && strlen(label) > 0 && strlen(label) < sizeof(label)) {
                LOG_INFO("[VoicemeeterManager::ListAllChannels] | " + std::to_string(index) +
                         " | " + std::string(label) + " | " + type +
                         " |");
            } else {
                LOG_INFO("[VoicemeeterManager::ListAllChannels] | " + std::to_string(index) +
                         " | N/A | " + type + " |");
            }
        } else {
            LOG_ERROR("[VoicemeeterManager::ListAllChannels] VBVMR_GetParameterStringA is not available.");
            LOG_INFO("[VoicemeeterManager::ListAllChannels] | " + std::to_string(index) +
                     " | N/A | " + type + " |");
        }
    };

    LOG_INFO("[VoicemeeterManager::ListAllChannels] \nStrips:");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] | Index   | Label                | Type         |");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");

    for (int i = 0; i < maxStrips; ++i) {
        std::string paramName = "Strip[" + std::to_string(i) + "].Label";
        PrintParameter(paramName, "Input Strip", i);
    }
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");

    LOG_INFO("[VoicemeeterManager::ListAllChannels] \nBuses:");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] | Index   | Label                | Type         |");
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");

    for (int i = 0; i < maxBuses; ++i) {
        std::string paramNameBus = "Bus[" + std::to_string(i) + "].Label";
        std::string busType = "BUS " + std::to_string(i);
        PrintParameter(paramNameBus, busType, i);
    }
    LOG_INFO("[VoicemeeterManager::ListAllChannels] +---------+----------------------+--------------+");
}

void VoicemeeterManager::ListInputs() {
    LOG_DEBUG("[VoicemeeterManager::ListInputs] Listing Voicemeeter inputs.");

    try {
        int maxStrips = 8;
        LOG_INFO("[VoicemeeterManager::ListInputs] Available Voicemeeter Virtual Inputs:");

        for (int i = 0; i < maxStrips; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512] = {0};
            if (VBVMR_GetParameterStringA) {
                long result = VBVMR_GetParameterStringA(paramName, label);
                if (result == 0 && strlen(label) > 0) {
                    LOG_INFO("[VoicemeeterManager::ListInputs] " + std::to_string(i) + ": " + std::string(label));
                } else {
                    LOG_INFO("[VoicemeeterManager::ListInputs] " + std::to_string(i) + ": N/A");
                    break;
                }
            } else {
                LOG_ERROR("[VoicemeeterManager::ListInputs] VBVMR_GetParameterStringA is not available.");
                break;
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("[VoicemeeterManager::ListInputs] Error listing Voicemeeter inputs: " + std::string(ex.what()));
    }
}

void VoicemeeterManager::ListOutputs() {
    LOG_DEBUG("[VoicemeeterManager::ListOutputs] Listing Voicemeeter outputs.");

    try {
        int maxBuses = 8;
        LOG_INFO("[VoicemeeterManager::ListOutputs] Available Voicemeeter Virtual Outputs:");

        for (int i = 0; i < maxBuses; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256] = {0};
            if (VBVMR_GetParameterStringA) {
                long result = VBVMR_GetParameterStringA(paramName, label);
                if (result == 0 && strlen(label) > 0) {
                    LOG_INFO("[VoicemeeterManager::ListOutputs] " + std::to_string(i) + ": " + std::string(label));
                } else {
                    LOG_INFO("[VoicemeeterManager::ListOutputs] " + std::to_string(i) + ": N/A");
                    break;
                }
            } else {
                LOG_ERROR("[VoicemeeterManager::ListOutputs] VBVMR_GetParameterStringA is not available.");
                break;
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("[VoicemeeterManager::ListOutputs] Error listing Voicemeeter outputs: " + std::string(ex.what()));
    }
}

bool VoicemeeterManager::GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted) {
    LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Getting volume and mute state for channel index: " + std::to_string(channelIndex));

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
        LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Gain parameter retrieved: " + std::to_string(gainValue) + " dBm (" + std::to_string(volumePercent) + "%)");
    } else {
        LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Failed to get Gain parameter for " + gainParam);
        return false;
    }

    if (VBVMR_GetParameterFloat &&
        VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue) == 0) {
        isMuted = (muteValue != 0.0f);
        LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Mute parameter retrieved: " + std::to_string(muteValue) + " (" + (isMuted ? "Muted" : "Unmuted") + ")");
    } else {
        LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Failed to get Mute parameter for " + muteParam);
        return false;
    }

    LOG_DEBUG("[VoicemeeterManager::GetVoicemeeterVolume] Volume: " + std::to_string(volumePercent) + "% (" + std::to_string(gainValue) + " dBm) " + (isMuted ? "(Muted)" : "(Unmuted)"));
    return true;
}

void VoicemeeterManager::UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted) {
    LOG_DEBUG("[VoicemeeterManager::UpdateVoicemeeterVolume] Updating volume and mute state for channel index: " + std::to_string(channelIndex) +
              " to " + std::to_string(volumePercent) + "% and " + (isMuted ? "Muted" : "Unmuted") + ".");

    std::lock_guard<std::mutex> lock(channelMutex);

    if (!VBVMR_SetParameterFloat) {
        LOG_ERROR("[VoicemeeterManager::UpdateVoicemeeterVolume] VBVMR_SetParameterFloat is not available.");
        return;
    }

    float dBmValue = VolumeUtils::PercentToDbm(volumePercent);
    LOG_DEBUG("[VoicemeeterManager::UpdateVoicemeeterVolume] Converted " + std::to_string(volumePercent) + "% to " + std::to_string(dBmValue) + " dBm.");

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
        LOG_ERROR("[VoicemeeterManager::UpdateVoicemeeterVolume] Failed to set Gain parameter for " + gainParam);
    } else {
        LOG_DEBUG("[VoicemeeterManager::UpdateVoicemeeterVolume] Gain parameter set to " + std::to_string(dBmValue) + " dBm (" + std::to_string(volumePercent) + "%).");
    }

    if (VBVMR_SetParameterFloat(const_cast<char*>(muteParam.c_str()), isMuted ? 1.0f : 0.0f) != 0) {
        LOG_ERROR("[VoicemeeterManager::UpdateVoicemeeterVolume] Failed to set Mute parameter for " + muteParam);
    } else {
        LOG_DEBUG("[VoicemeeterManager::UpdateVoicemeeterVolume] Mute parameter set to " + std::string(isMuted ? "Muted" : "Unmuted") + ".");
    }

    LOG_DEBUG("[VoicemeeterManager::UpdateVoicemeeterVolume] Voicemeeter volume updated: " + std::to_string(volumePercent) + "% (" +
              std::to_string(dBmValue) + " dBm) " + (isMuted ? "(Muted)" : "(Unmuted)"));
}

bool VoicemeeterManager::IsParametersDirty() {
    LOG_DEBUG("[VoicemeeterManager::IsParametersDirty] Checking if parameters are dirty.");

    if (!loggedIn) {
        LOG_ERROR("[VoicemeeterManager::IsParametersDirty] Cannot check parameters dirty state: not logged in.");
        return false;
    }

    if (!VBVMR_IsParametersDirty) {
        LOG_ERROR("[VoicemeeterManager::IsParametersDirty] VBVMR_IsParametersDirty is not available.");
        return false;
    }

    long result = VBVMR_IsParametersDirty();
    LOG_DEBUG("[VoicemeeterManager::IsParametersDirty] VBVMR_IsParametersDirty result: " + std::to_string(result));

    switch (result) {
        case 0:
            LOG_DEBUG("[VoicemeeterManager::IsParametersDirty] Parameters have not changed (not dirty).");
            return false;
        case 1:
            LOG_DEBUG("[VoicemeeterManager::IsParametersDirty] Parameters have changed (dirty).");
            return true;
        case -1:
            LOG_ERROR("[VoicemeeterManager::IsParametersDirty] Unexpected error occurred.");
            return false;
        case -2:
            LOG_ERROR("[VoicemeeterManager::IsParametersDirty] No Voicemeeter server detected.");
            return false;
        default:
            LOG_ERROR("[VoicemeeterManager::IsParametersDirty] Unknown result code: " + std::to_string(result));
            return false;
    }
}

bool VoicemeeterManager::GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent) {
    LOG_DEBUG("[VoicemeeterManager::GetChannelVolume] Getting volume for channel index: " + std::to_string(channelIndex));

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
        LOG_DEBUG("[VoicemeeterManager::GetChannelVolume] Channel " + std::to_string(channelIndex) +
                  " Volume: " + std::to_string(volumePercent) + "% (" + std::to_string(gainValue) + " dBm)");
        return true;
    } else {
        LOG_DEBUG("[VoicemeeterManager::GetChannelVolume] Failed to get Gain parameter for " + gainParam);
        return false;
    }
}

bool VoicemeeterManager::IsChannelMuted(int channelIndex, ChannelType channelType) {
    LOG_DEBUG("[VoicemeeterManager::IsChannelMuted] Checking mute state for channel index: " + std::to_string(channelIndex));

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
        LOG_DEBUG("[VoicemeeterManager::IsChannelMuted] Channel " + std::to_string(channelIndex) +
                  " Mute State: " + (isMuted ? "Muted" : "Unmuted"));
        return isMuted;
    } else {
        LOG_DEBUG("[VoicemeeterManager::IsChannelMuted] Failed to get Mute parameter for " + muteParam);
        return false;
    }
}

bool VoicemeeterManager::SetMute(int channelIndex, ChannelType channelType, bool isMuted) {
    LOG_DEBUG("[VoicemeeterManager::SetMute] Setting mute state for channel index: " + std::to_string(channelIndex) +
              " to " + (isMuted ? "Muted" : "Unmuted") + ".");
    std::lock_guard<std::mutex> lock(channelMutex);
    return SetMuteInternal(channelIndex, channelType, isMuted);
}

bool VoicemeeterManager::SetMuteInternal(int channelIndex, ChannelType channelType, bool isMuted) {
    if (!VBVMR_SetParameterFloat) {
        LOG_ERROR("[VoicemeeterManager::SetMuteInternal] VBVMR_SetParameterFloat is not available.");
        return false;
    }

    std::string muteParam;

    if (channelType == ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    float muteValue = isMuted ? 1.0f : 0.0f;
    LOG_DEBUG("[VoicemeeterManager::SetMuteInternal] Setting " + muteParam + " to " + std::to_string(muteValue));

    long result = VBVMR_SetParameterFloat(const_cast<char*>(muteParam.c_str()), muteValue);

    if (result != 0) {
        LOG_ERROR("[VoicemeeterManager::SetMuteInternal] Failed to set Mute parameter for " + muteParam +
                  ". Error code: " + std::to_string(result));
        return false;
    }

    LOG_DEBUG("[VoicemeeterManager::SetMuteInternal] Channel " + std::to_string(channelIndex) +
              " mute state set to " + (isMuted ? "Muted" : "Unmuted") + ".");
    return true;
}