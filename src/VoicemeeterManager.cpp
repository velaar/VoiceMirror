// VoicemeeterManager.cpp

#include "VoicemeeterManager.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include "Defconf.h"

// Constructor
VoicemeeterManager::VoicemeeterManager()
    : hVoicemeeterRemote(NULL),
      VBVMR_Login(nullptr),
      VBVMR_Logout(nullptr),
      VBVMR_RunVoicemeeter(nullptr),
      VBVMR_GetVoicemeeterType(nullptr),
      VBVMR_GetVoicemeeterVersion(nullptr),
      VBVMR_IsParametersDirty(nullptr),
      VBVMR_GetParameterFloat(nullptr),
      VBVMR_GetParameterStringA(nullptr),
      VBVMR_GetParameterStringW(nullptr),
      /* VBVMR_GetLevel(nullptr),
      VBVMR_GetMidiMessage(nullptr),
      VBVMR_SendMidiMessage(nullptr),
      VBVMR_SetParameters(nullptr),
      VBVMR_SetParametersW(nullptr),
      VBVMR_SetParameterStringA(nullptr),
      VBVMR_SetParameterStringW(nullptr),
      VBVMR_Output_GetDeviceNumber(nullptr),
      VBVMR_Output_GetDeviceDescA(nullptr),
      VBVMR_Output_GetDeviceDescW(nullptr),
      VBVMR_Input_GetDeviceNumber(nullptr),
      VBVMR_Input_GetDeviceDescA(nullptr),
      VBVMR_Input_GetDeviceDescW(nullptr), */
      /* VBVMR_AudioCallbackRegister(nullptr),
      VBVMR_AudioCallbackStart(nullptr),
      VBVMR_AudioCallbackStop(nullptr),
      VBVMR_AudioCallbackUnregister(nullptr), */
      /* VBVMR_MacroButton_IsDirty(nullptr),
      VBVMR_MacroButton_GetStatus(nullptr),
      VBVMR_MacroButton_SetStatus(nullptr), */
      initialized(false),
      loggedIn(false),
      debugMode(false),
      comInitialized(false) {}

// Destructor
VoicemeeterManager::~VoicemeeterManager() {
    Shutdown();
    UninitializeCOM();
}

// Initialize COM library
bool VoicemeeterManager::InitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitMutex);

    if (!comInitialized) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        
        if (SUCCEEDED(hr)) {
            comInitialized = true;
            LOG_DEBUG("COM library initialized successfully.");
            return true;
        }
        else if (hr == RPC_E_CHANGED_MODE) {
            LOG_DEBUG("COM already initialized with different threading model.");
            return true;
        }
        else {
            LOG_ERROR("Failed to initialize COM library. HRESULT: " + std::to_string(hr));
            return false;
        }
    }
    return true;
}

// Uninitialize COM library
void VoicemeeterManager::UninitializeCOM() {
    if (comInitialized) {
        CoUninitialize();
        comInitialized = false;
        LOG_DEBUG("COM library uninitialized.");
    }
}

// Load VoicemeeterRemote DLL and retrieve function pointers
bool VoicemeeterManager::LoadVoicemeeterRemote() {
    if (initialized) {
        LOG_DEBUG("VoicemeeterRemote DLL is already loaded.");
        return true;
    }

    // Use the correct DLL path based on architecture
#ifdef _WIN64
    const char* dllFullPath = DEFAULT_DLL_PATH_64;
#else
    const char* dllFullPath = DEFAULT_DLL_PATH_32;
#endif

    LOG_DEBUG("Loading VoicemeeterRemote DLL from: " + std::string(dllFullPath));

    hVoicemeeterRemote = LoadLibraryA(dllFullPath);
    if (hVoicemeeterRemote == NULL) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to load VoicemeeterRemote DLL. Error code: " + std::to_string(error));
        return false;
    }

    // Retrieve all required function pointers
    VBVMR_Login = (T_VBVMR_Login)GetProcAddress(hVoicemeeterRemote, "VBVMR_Login");
    VBVMR_Logout = (T_VBVMR_Logout)GetProcAddress(hVoicemeeterRemote, "VBVMR_Logout");
    VBVMR_RunVoicemeeter = (T_VBVMR_RunVoicemeeter)GetProcAddress(hVoicemeeterRemote, "VBVMR_RunVoicemeeter");
    VBVMR_GetVoicemeeterType = (T_VBVMR_GetVoicemeeterType)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetVoicemeeterType");
    VBVMR_GetVoicemeeterVersion = (T_VBVMR_GetVoicemeeterVersion)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetVoicemeeterVersion");
    VBVMR_IsParametersDirty = (T_VBVMR_IsParametersDirty)GetProcAddress(hVoicemeeterRemote, "VBVMR_IsParametersDirty");
    VBVMR_GetParameterFloat = (T_VBVMR_GetParameterFloat)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetParameterFloat");
    VBVMR_GetParameterStringA = (T_VBVMR_GetParameterStringA)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetParameterStringA");
    VBVMR_GetParameterStringW = (T_VBVMR_GetParameterStringW)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetParameterStringW");
    /* VBVMR_GetLevel = (T_VBVMR_GetLevel)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetLevel");
    VBVMR_GetMidiMessage = (T_VBVMR_GetMidiMessage)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetMidiMessage");
    VBVMR_SendMidiMessage = (T_VBVMR_SendMidiMessage)GetProcAddress(hVoicemeeterRemote, "VBVMR_SendMidiMessage");
    VBVMR_SetParameters = (T_VBVMR_SetParameters)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameters");
    VBVMR_SetParametersW = (T_VBVMR_SetParametersW)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParametersW");
    VBVMR_SetParameterStringA = (T_VBVMR_SetParameterStringA)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameterStringA");
    VBVMR_SetParameterStringW = (T_VBVMR_SetParameterStringW)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameterStringW");
    VBVMR_Output_GetDeviceNumber = (T_VBVMR_Output_GetDeviceNumber)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceNumber");
    VBVMR_Output_GetDeviceDescA = (T_VBVMR_Output_GetDeviceDescA)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceDescA");
    VBVMR_Output_GetDeviceDescW = (T_VBVMR_Output_GetDeviceDescW)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceDescW");
    VBVMR_Input_GetDeviceNumber = (T_VBVMR_Input_GetDeviceNumber)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceNumber");
    VBVMR_Input_GetDeviceDescA = (T_VBVMR_Input_GetDeviceDescA)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceDescA");
    VBVMR_Input_GetDeviceDescW = (T_VBVMR_Input_GetDeviceDescW)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceDescW"); */

    // Verify all required functions are loaded
    if (!VBVMR_Login || !VBVMR_Logout || !VBVMR_RunVoicemeeter ||
        !VBVMR_GetVoicemeeterType || !VBVMR_GetVoicemeeterVersion ||
        !VBVMR_IsParametersDirty || !VBVMR_GetParameterFloat ||
        !VBVMR_GetParameterStringA || !VBVMR_GetParameterStringW /* ||
        !VBVMR_GetLevel || !VBVMR_GetMidiMessage || !VBVMR_SendMidiMessage ||
        !VBVMR_SetParameters || !VBVMR_SetParametersW || !VBVMR_SetParameterStringA ||
        !VBVMR_SetParameterStringW || !VBVMR_Output_GetDeviceNumber ||
        !VBVMR_Output_GetDeviceDescA || !VBVMR_Output_GetDeviceDescW ||
        !VBVMR_Input_GetDeviceNumber || !VBVMR_Input_GetDeviceDescA ||
        !VBVMR_Input_GetDeviceDescW */) {
        LOG_ERROR("Failed to retrieve all required function pointers from VoicemeeterRemote DLL.");
        UnloadVoicemeeterRemote();
        return false;
    }

    LOG_DEBUG("VoicemeeterRemote DLL loaded and all function pointers retrieved successfully.");
    initialized = true;
    return true;
}

// Unload VoicemeeterRemote DLL
void VoicemeeterManager::UnloadVoicemeeterRemote() {
    if (hVoicemeeterRemote) {
        FreeLibrary(hVoicemeeterRemote);
        hVoicemeeterRemote = NULL;
        LOG_DEBUG("VoicemeeterRemote DLL unloaded.");
    }

    // Reset all used function pointers
    VBVMR_Login = nullptr;
    VBVMR_Logout = nullptr;
    VBVMR_RunVoicemeeter = nullptr;
    VBVMR_GetVoicemeeterType = nullptr;
    VBVMR_GetVoicemeeterVersion = nullptr;
    VBVMR_IsParametersDirty = nullptr;
    VBVMR_GetParameterFloat = nullptr;
    VBVMR_GetParameterStringA = nullptr;
    VBVMR_GetParameterStringW = nullptr;
    /* VBVMR_GetLevel = nullptr;
    VBVMR_GetMidiMessage = nullptr;
    VBVMR_SendMidiMessage = nullptr;
    VBVMR_SetParameters = nullptr;
    VBVMR_SetParametersW = nullptr;
    VBVMR_SetParameterStringA = nullptr;
    VBVMR_SetParameterStringW = nullptr;
    VBVMR_Output_GetDeviceNumber = nullptr;
    VBVMR_Output_GetDeviceDescA = nullptr;
    VBVMR_Output_GetDeviceDescW = nullptr;
    VBVMR_Input_GetDeviceNumber = nullptr;
    VBVMR_Input_GetDeviceDescA = nullptr;
    VBVMR_Input_GetDeviceDescW = nullptr; */

    /* VBVMR_AudioCallbackRegister = nullptr;
    VBVMR_AudioCallbackStart = nullptr;
    VBVMR_AudioCallbackStop = nullptr;
    VBVMR_AudioCallbackUnregister = nullptr;

    VBVMR_MacroButton_IsDirty = nullptr;
    VBVMR_MacroButton_GetStatus = nullptr;
    VBVMR_MacroButton_SetStatus = nullptr; */

    initialized = false;
}

// Initialize the Voicemeeter API and log in
bool VoicemeeterManager::Initialize(int voicemeeterType) {
    if (loggedIn) {
        LOG_DEBUG("Voicemeeter is already logged in.");
        return true;
    }

    LOG_DEBUG("Initializing Voicemeeter Manager...");

    if (!InitializeCOM()) {  // Initialize COM before using Voicemeeter API
        LOG_ERROR("COM initialization failed.");
        return false;
    }

    if (!LoadVoicemeeterRemote()) {
        LOG_ERROR("Failed to load VoicemeeterRemote DLL.");
        UninitializeCOM();  // Clean up COM if DLL loading fails
        return false;
    }

    // Attempt to log in
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
            UninitializeCOM();  // Clean up COM if running Voicemeeter fails
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        loginResult = VBVMR_Login();
        LOG_DEBUG("Voicemeeter login result after running: " +
                  std::to_string(loginResult));
        loggedIn = (loginResult == -2 || loginResult == 0);

        // Check again if Voicemeeter is running
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

            if (audioEngineRunning) break;

            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }

        if (!audioEngineRunning) {
            LOG_ERROR("Voicemeeter audio engine check failed after " +
                      std::to_string(MAX_RETRIES) + " attempts. Engine not responsive.");
            Shutdown();
            return false;
        }

        LOG_DEBUG("Voicemeeter login and audio engine confirmation successful.");

        // Initialize based on Voicemeeter type
        std::string typeStr;
        int maxStrips = 0;
        int maxBuses = 0;
        // Set limits based on Voicemeeter type
        switch (vmType) {
            case 1:
                typeStr = "Voicemeeter";
                maxStrips = 3;  // Including virtual inputs
                maxBuses = 2;
                break;
            case 2:
                typeStr = "Voicemeeter Banana";
                maxStrips = 5;  // Including virtual inputs
                maxBuses = 5;
                break;
            case 3:
                typeStr = "Voicemeeter Potato";
                maxStrips = 8;  // Including virtual inputs
                maxBuses = 8;
                break;
            default:
                LOG_ERROR("Unknown Voicemeeter type.");
                Shutdown();
                return false;
        }

        LOG_INFO("Voicemeeter Type: " + typeStr);

        // You can perform additional initialization here if needed
    } else {
        LOG_ERROR("Voicemeeter login failed.");
        UnloadVoicemeeterRemote();
        UninitializeCOM();  // Clean up COM if login fails
    }

    return loggedIn;
}

// Shutdown the Voicemeeter API and log out
void VoicemeeterManager::Shutdown() {
    if (loggedIn) {
        LOG_DEBUG("Shutting down Voicemeeter.");
        VBVMR_Logout();
        UnloadVoicemeeterRemote();
        loggedIn = false;
    }
    UninitializeCOM();
}

// Sends a shutdown command to Voicemeeter
void VoicemeeterManager::ShutdownCommand() {
    LOG_DEBUG("Sending shutdown command to Voicemeeter.");
    VBVMR_SetParameterFloat("Command.Shutdown", 1.0f);
}

// Restarts the Voicemeeter audio engine
void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    LOG_DEBUG("Restarting Voicemeeter audio engine...");

    // Delay before restarting
    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    VBVMR_SetParameterFloat("Command.Restart", 1.0f);

    // Delay after restarting
    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));

    LOG_DEBUG("Voicemeeter audio engine restarted.");
}

// Sets the debug mode
void VoicemeeterManager::SetDebugMode(bool newDebugMode) {
    this->debugMode = newDebugMode;
    // Optionally, adjust logger verbosity based on debug mode
}

// Gets the current debug mode state
bool VoicemeeterManager::GetDebugMode() {
    return debugMode;
}

// Lists all Voicemeeter channels with labels
void VoicemeeterManager::ListAllChannels() {
    long voicemeeterType = 0;
    HRESULT hr = VBVMR_GetVoicemeeterType(&voicemeeterType);
    if (hr != 0) {
        LOG_ERROR("Failed to get Voicemeeter type.");
        return;
    }

    std::string typeStr;
    int maxStrips = 0;
    int maxBuses = 0;

    // Set limits based on Voicemeeter type
    switch (voicemeeterType) {
        case 1:
            typeStr = "Voicemeeter";
            maxStrips = 3;  // Including virtual inputs
            maxBuses = 2;
            break;
        case 2:
            typeStr = "Voicemeeter Banana";
            maxStrips = 5;  // Including virtual inputs
            maxBuses = 5;
            break;
        case 3:
            typeStr = "Voicemeeter Potato";
            maxStrips = 8;  // Including virtual inputs
            maxBuses = 8;
            break;
        default:
            LOG_ERROR("Unknown Voicemeeter type.");
            return;
    }

    LOG_INFO("Voicemeeter Type: " + typeStr);
    // Lambda to print channel parameters
    auto PrintParameter = [&](const std::string& param, const std::string& type, int index) {
        char label[512] = {0};
        long result = VBVMR_GetParameterStringA(const_cast<char*>(param.c_str()), label);
        if (result == 0 && strlen(label) > 0 && strlen(label) < sizeof(label)) {
            LOG_INFO("| " + std::to_string(index) +
                     " | " + label + " | " + type +
                     " |");
        } else {
            LOG_INFO("| " + std::to_string(index) +
                     " | N/A | " + type + " |");
        }
    };

    // Print Strips
    LOG_INFO("\nStrips:");
    LOG_INFO("+---------+----------------------+--------------+");
    LOG_INFO("| Index   | Label                | Type         |");
    LOG_INFO("+---------+----------------------+--------------+");

    for (int i = 0; i < maxStrips; ++i) {
        std::string paramName = "Strip[" + std::to_string(i) + "].Label";
        PrintParameter(paramName, "Input Strip", i);
    }
    LOG_INFO("+---------+----------------------+--------------+");

    // Print Buses
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

// Lists Voicemeeter virtual inputs
void VoicemeeterManager::ListInputs() {
    try {
        int maxStrips = 8;  // Maximum number of strips to check
        LOG_INFO("Available Voicemeeter Virtual Inputs:");

        for (int i = 0; i < maxStrips; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512];
            long result = VBVMR_GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0) {
                LOG_INFO(std::to_string(i) + ": " + label);
            } else {
                break;  // No more strips
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Error listing Voicemeeter inputs: " + std::string(ex.what()));
    }
}

// Lists Voicemeeter virtual outputs
void VoicemeeterManager::ListOutputs() {
    try {
        int maxBuses = 8;  // Maximum number of buses to check
        LOG_INFO("Available Voicemeeter Virtual Outputs:");

        for (int i = 0; i < maxBuses; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256];
            long result = VBVMR_GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0) {
                LOG_INFO(std::to_string(i) + ": " + label);
            } else {
                break;  // No more buses
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Error listing Voicemeeter outputs: " + std::string(ex.what()));
    }
}

// Retrieves the current volume and mute state of a channel
bool VoicemeeterManager::GetVoicemeeterVolume(int channelIndex, ChannelType channelType, float& volumePercent, bool& isMuted) {
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

    if (VBVMR_GetParameterFloat(const_cast<char*>(gainParam.c_str()), &gainValue) != 0) {
        LOG_DEBUG("Failed to get Gain parameter for " + gainParam);
        return false;
    }

    if (VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue) != 0) {
        LOG_DEBUG("Failed to get Mute parameter for " + muteParam);
        return false;
    }

    volumePercent = VolumeUtils::dBmToPercent(gainValue);
    isMuted = (muteValue != 0.0f);

    LOG_DEBUG("Volume: " + std::to_string(volumePercent) + "% " + (isMuted ? "(Muted)" : "(Unmuted)"));
    return true;
}

// Updates the Voicemeeter volume and mute state of a channel
void VoicemeeterManager::UpdateVoicemeeterVolume(int channelIndex, ChannelType channelType, float volumePercent, bool isMuted) {
    float dBmValue = VolumeUtils::PercentToDbm(volumePercent);  // Use VolumeUtils

    std::string gainParam;
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {  // ChannelType::Output
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

// Checks if any Voicemeeter parameters have changed
bool VoicemeeterManager::IsParametersDirty() {
    if (!loggedIn) {
        LOG_ERROR("Cannot check parameters dirty state: not logged in.");
        return false;
    }

    long result = VBVMR_IsParametersDirty();
    switch (result) {
        case 0:  // No new parameters
            LOG_DEBUG("IsParametersDirty: Parameters have not changed (not dirty).");
            return false;
        case 1:  // Parameters have changed
            LOG_DEBUG("IsParametersDirty: Parameters have changed (dirty).");
            return true;

        case -1:  // Unexpected error
            LOG_ERROR("IsParametersDirty: Unexpected error occurred.");
            return false;
        case -2:  // No server
            LOG_ERROR("IsParametersDirty: No Voicemeeter server detected.");
            return false;
        default:
            LOG_ERROR("IsParametersDirty: Unknown result code: " + std::to_string(result));
            return false;
    }
}

// **Implementation of GetChannelVolume**
bool VoicemeeterManager::GetChannelVolume(int channelIndex, ChannelType channelType, float& volumePercent) {
    std::lock_guard<std::mutex> lock(channelMutex);

    float gainValue = 0.0f;
    std::string gainParam;

    if (channelType == ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
    } else {  // ChannelType::Output
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
    }

    long result = VBVMR_GetParameterFloat(const_cast<char*>(gainParam.c_str()), &gainValue);
    if (result != 0) {
        LOG_DEBUG("Failed to get Gain parameter for " + gainParam);
        return false;
    }

    volumePercent = VolumeUtils::dBmToPercent(gainValue);
    LOG_DEBUG("Channel " + std::to_string(channelIndex) + " Volume: " + std::to_string(volumePercent) + "%");
    return true;
}

// **Implementation of IsChannelMuted**
bool VoicemeeterManager::IsChannelMuted(int channelIndex, ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelMutex);

    float muteValue = 0.0f;
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {  // ChannelType::Output
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    long result = VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue);
    if (result != 0) {
        LOG_DEBUG("Failed to get Mute parameter for " + muteParam);
        return false;
    }

    bool isMuted = (muteValue != 0.0f);
    LOG_DEBUG("Channel " + std::to_string(channelIndex) + " Mute State: " + (isMuted ? "Muted" : "Unmuted"));
    return isMuted;
}

// **Commenting Out Unused Audio Callback Functions**
// If your application does not utilize audio callbacks, you can comment out the following functions.
// This helps in maintaining a cleaner codebase.

// Example:

// // Registers an audio callback
// long VoicemeeterManager::RegisterAudioCallback(long mode, std::function<void(void*, long, void*, long)> pCallback, void* lpUser, char szClientName[64]) {
//     return VBVMR_AudioCallbackRegister(mode, pCallback, lpUser, szClientName);
// }

// // Starts audio callbacks
// long VoicemeeterManager::StartAudioCallbacks() {
//     return VBVMR_AudioCallbackStart();
// }

// // Stops audio callbacks
// long VoicemeeterManager::StopAudioCallbacks() {
//     return VBVMR_AudioCallbackStop();
// }

// // Unregisters audio callbacks
// long VoicemeeterManager::UnregisterAudioCallback() {
//     return VBVMR_AudioCallbackUnregister();
// }

// // Checks if MacroButton parameters are dirty
// bool VoicemeeterManager::IsMacroButtonDirty() {
//     long result = VBVMR_MacroButton_IsDirty();
//     return (result == 1);
// }

// // Gets the status of a MacroButton
// bool VoicemeeterManager::GetMacroButtonStatus(long nuLogicalButton, float& pValue, long bitmode) {
//     return (VBVMR_MacroButton_GetStatus(nuLogicalButton, &pValue, bitmode) == 0);
// }

// // Sets the status of a MacroButton
// bool VoicemeeterManager::SetMacroButtonStatus(long nuLogicalButton, float fValue, long bitmode) {
//     return (VBVMR_MacroButton_SetStatus(nuLogicalButton, fValue, bitmode) == 0);
// }


// Implement ListMonitorableDevices method without using std::wstring_convert
void VoicemeeterManager::ListMonitorableDevices() {
    try {
        LOG_INFO("Listing Monitorable Devices:");

        // Example: Listing all audio endpoints using MMDevice API
        // This requires proper COM initialization, which should already be handled in Initialize()

        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf())
        );

        if (FAILED(hr)) {
            LOG_ERROR("Failed to create IMMDeviceEnumerator. HRESULT: " + std::to_string(hr));
            return;
        }

        ComPtr<IMMDeviceCollection> deviceCollection;
        hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to enumerate audio endpoints. HRESULT: " + std::to_string(hr));
            return;
        }

        UINT deviceCount;
        hr = deviceCollection->GetCount(&deviceCount);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get device count. HRESULT: " + std::to_string(hr));
            return;
        }

        LOG_INFO("+---------+----------------------+");
        LOG_INFO("| Index   | Device Name           |");
        LOG_INFO("+---------+----------------------+");

        for (UINT i = 0; i < deviceCount; ++i) {
            ComPtr<IMMDevice> device;
            hr = deviceCollection->Item(i, &device);
            if (FAILED(hr)) {
                LOG_ERROR("Failed to get device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
                continue;
            }

            ComPtr<IPropertyStore> propertyStore;
            hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
            if (FAILED(hr)) {
                LOG_ERROR("Failed to open property store for device " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
                continue;
            }

            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
            if (FAILED(hr)) {
                LOG_ERROR("Failed to get device name for device " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
                PropVariantClear(&varName);
                continue;
            }

            std::wstring deviceName(varName.pwszVal);
            std::string deviceNameStr;

            // Convert std::wstring to std::string using WideCharToMultiByte
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, deviceName.c_str(), -1, NULL, 0, NULL, NULL);
            if (sizeNeeded > 0) {
                deviceNameStr.resize(sizeNeeded - 1); // Exclude null terminator
                WideCharToMultiByte(CP_UTF8, 0, deviceName.c_str(), -1, &deviceNameStr[0], sizeNeeded, NULL, NULL);
            } else {
                deviceNameStr = "Unknown Device";
            }

            LOG_INFO("| " + std::to_string(i) + " | " + deviceNameStr + " |");
            PropVariantClear(&varName);
        }

        LOG_INFO("+---------+----------------------+");
    } catch (const std::exception& ex) {
        LOG_ERROR("Exception in ListMonitorableDevices: " + std::string(ex.what()));
    }
}

bool VoicemeeterManager::SetMute(int channelIndex, ChannelType channelType, bool isMuted) {
    std::lock_guard<std::mutex> lock(channelMutex); // Ensure thread safety
    return SetMuteInternal(channelIndex, channelType, isMuted);
}

bool VoicemeeterManager::SetMuteInternal(int channelIndex, ChannelType channelType, bool isMuted) {
    std::string muteParam;

    if (channelType == ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else { // ChannelType::Output
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
