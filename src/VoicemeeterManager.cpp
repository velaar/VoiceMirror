// VoicemeeterManager.cpp

#include "VoicemeeterManager.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "defconf.h"

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
      VBVMR_GetLevel(nullptr),
      VBVMR_GetMidiMessage(nullptr),
      VBVMR_SendMidiMessage(nullptr),
      VBVMR_SetParameterFloat(nullptr),
      VBVMR_SetParameters(nullptr),
      VBVMR_SetParametersW(nullptr),
      VBVMR_SetParameterStringA(nullptr),
      VBVMR_SetParameterStringW(nullptr),
      VBVMR_Output_GetDeviceNumber(nullptr),
      VBVMR_Output_GetDeviceDescA(nullptr),
      VBVMR_Output_GetDeviceDescW(nullptr),
      VBVMR_Input_GetDeviceNumber(nullptr),
      VBVMR_Input_GetDeviceDescA(nullptr),
      VBVMR_Input_GetDeviceDescW(nullptr),
      VBVMR_AudioCallbackRegister(nullptr),
      VBVMR_AudioCallbackStart(nullptr),
      VBVMR_AudioCallbackStop(nullptr),
      VBVMR_AudioCallbackUnregister(nullptr),
      VBVMR_MacroButton_IsDirty(nullptr),
      VBVMR_MacroButton_GetStatus(nullptr),
      VBVMR_MacroButton_SetStatus(nullptr),
      initialized(false),
      loggedIn(false),
      debugMode(false),
      comInitialized(false) {}

// Destructor
VoicemeeterManager::~VoicemeeterManager() {
    Shutdown();
}

// Initialize COM library
bool VoicemeeterManager::InitializeCOM() {
    std::lock_guard<std::mutex> lock(comInitMutex);  // Ensure thread safety

    if (!comInitialized) {
        HRESULT hr = CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED  // Use apartment-threaded model
        );
        if (hr == RPC_E_CHANGED_MODE) {
            LOG_WARNING("COM library already initialized with a different threading model.");
            comInitialized = true;
            return true;
        } else if (FAILED(hr)) {
            LOG_ERROR("Failed to initialize COM library. HRESULT: " + std::to_string(hr));
            return false;
        }
        comInitialized = true;
        LOG_DEBUG("COM library initialized successfully.");
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
    VBVMR_GetLevel = (T_VBVMR_GetLevel)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetLevel");
    VBVMR_GetMidiMessage = (T_VBVMR_GetMidiMessage)GetProcAddress(hVoicemeeterRemote, "VBVMR_GetMidiMessage");
    VBVMR_SendMidiMessage = (T_VBVMR_SendMidiMessage)GetProcAddress(hVoicemeeterRemote, "VBVMR_SendMidiMessage");
    VBVMR_SetParameterFloat = (T_VBVMR_SetParameterFloat)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameterFloat");
    VBVMR_SetParameters = (T_VBVMR_SetParameters)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameters");
    VBVMR_SetParametersW = (T_VBVMR_SetParametersW)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParametersW");
    VBVMR_SetParameterStringA = (T_VBVMR_SetParameterStringA)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameterStringA");
    VBVMR_SetParameterStringW = (T_VBVMR_SetParameterStringW)GetProcAddress(hVoicemeeterRemote, "VBVMR_SetParameterStringW");
    VBVMR_Output_GetDeviceNumber = (T_VBVMR_Output_GetDeviceNumber)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceNumber");
    VBVMR_Output_GetDeviceDescA = (T_VBVMR_Output_GetDeviceDescA)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceDescA");
    VBVMR_Output_GetDeviceDescW = (T_VBVMR_Output_GetDeviceDescW)GetProcAddress(hVoicemeeterRemote, "VBVMR_Output_GetDeviceDescW");
    VBVMR_Input_GetDeviceNumber = (T_VBVMR_Input_GetDeviceNumber)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceNumber");
    VBVMR_Input_GetDeviceDescA = (T_VBVMR_Input_GetDeviceDescA)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceDescA");
    VBVMR_Input_GetDeviceDescW = (T_VBVMR_Input_GetDeviceDescW)GetProcAddress(hVoicemeeterRemote, "VBVMR_Input_GetDeviceDescW");
    VBVMR_AudioCallbackRegister = (T_VBVMR_AudioCallbackRegister)GetProcAddress(hVoicemeeterRemote, "VBVMR_AudioCallbackRegister");
    VBVMR_AudioCallbackStart = (T_VBVMR_AudioCallbackStart)GetProcAddress(hVoicemeeterRemote, "VBVMR_AudioCallbackStart");
    VBVMR_AudioCallbackStop = (T_VBVMR_AudioCallbackStop)GetProcAddress(hVoicemeeterRemote, "VBVMR_AudioCallbackStop");
    VBVMR_AudioCallbackUnregister = (T_VBVMR_AudioCallbackUnregister)GetProcAddress(hVoicemeeterRemote, "VBVMR_AudioCallbackUnregister");
    VBVMR_MacroButton_IsDirty = (T_VBVMR_MacroButton_IsDirty)GetProcAddress(hVoicemeeterRemote, "VBVMR_MacroButton_IsDirty");
    VBVMR_MacroButton_GetStatus = (T_VBVMR_MacroButton_GetStatus)GetProcAddress(hVoicemeeterRemote, "VBVMR_MacroButton_GetStatus");
    VBVMR_MacroButton_SetStatus = (T_VBVMR_MacroButton_SetStatus)GetProcAddress(hVoicemeeterRemote, "VBVMR_MacroButton_SetStatus");

    // Verify all required functions are loaded
    if (!VBVMR_Login || !VBVMR_Logout || !VBVMR_RunVoicemeeter ||
        !VBVMR_GetVoicemeeterType || !VBVMR_GetVoicemeeterVersion ||
        !VBVMR_IsParametersDirty || !VBVMR_GetParameterFloat ||
        !VBVMR_GetParameterStringA || !VBVMR_GetParameterStringW ||
        !VBVMR_GetLevel || !VBVMR_GetMidiMessage || !VBVMR_SendMidiMessage ||
        !VBVMR_SetParameterFloat || !VBVMR_SetParameters ||
        !VBVMR_SetParametersW || !VBVMR_SetParameterStringA ||
        !VBVMR_SetParameterStringW || !VBVMR_Output_GetDeviceNumber ||
        !VBVMR_Output_GetDeviceDescA || !VBVMR_Output_GetDeviceDescW ||
        !VBVMR_Input_GetDeviceNumber || !VBVMR_Input_GetDeviceDescA ||
        !VBVMR_Input_GetDeviceDescW || !VBVMR_AudioCallbackRegister ||
        !VBVMR_AudioCallbackStart || !VBVMR_AudioCallbackStop ||
        !VBVMR_AudioCallbackUnregister || !VBVMR_MacroButton_IsDirty ||
        !VBVMR_MacroButton_GetStatus || !VBVMR_MacroButton_SetStatus) {
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

    // Reset all function pointers
    VBVMR_Login = nullptr;
    VBVMR_Logout = nullptr;
    VBVMR_RunVoicemeeter = nullptr;
    VBVMR_GetVoicemeeterType = nullptr;
    VBVMR_GetVoicemeeterVersion = nullptr;
    VBVMR_IsParametersDirty = nullptr;
    VBVMR_GetParameterFloat = nullptr;
    VBVMR_GetParameterStringA = nullptr;
    VBVMR_GetParameterStringW = nullptr;
    VBVMR_GetLevel = nullptr;
    VBVMR_GetMidiMessage = nullptr;
    VBVMR_SendMidiMessage = nullptr;
    VBVMR_SetParameterFloat = nullptr;
    VBVMR_SetParameters = nullptr;
    VBVMR_SetParametersW = nullptr;
    VBVMR_SetParameterStringA = nullptr;
    VBVMR_SetParameterStringW = nullptr;
    VBVMR_Output_GetDeviceNumber = nullptr;
    VBVMR_Output_GetDeviceDescA = nullptr;
    VBVMR_Output_GetDeviceDescW = nullptr;
    VBVMR_Input_GetDeviceNumber = nullptr;
    VBVMR_Input_GetDeviceDescA = nullptr;
    VBVMR_Input_GetDeviceDescW = nullptr;
    VBVMR_AudioCallbackRegister = nullptr;
    VBVMR_AudioCallbackStart = nullptr;
    VBVMR_AudioCallbackStop = nullptr;
    VBVMR_AudioCallbackUnregister = nullptr;
    VBVMR_MacroButton_IsDirty = nullptr;
    VBVMR_MacroButton_GetStatus = nullptr;
    VBVMR_MacroButton_SetStatus = nullptr;

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
            float volumePercent = 0.0f;
            bool isMuted = false;

            // Hypothetical response code from GetVoicemeeterVolume for debugging purposes
            int responseCode = GetVoicemeeterVolume(0, VolumeUtils::ChannelType::Output, volumePercent, isMuted);

            switch (responseCode) {
                case 1:  // Success
                    audioEngineRunning = true;
                    LOG_DEBUG("Audio engine running on attempt " + std::to_string(attempt + 1) +
                              ". Initial bus gain: " + std::to_string(volumePercent) + "%" +
                              ", Mute state: " + (isMuted ? "Muted" : "Unmuted") + ".");
                    break;
                case -1:  // Device not found
                    LOG_WARNING("Attempt " + std::to_string(attempt + 1) +
                                ": Audio device not found. Response code: -1.");
                    break;
                case -2:  // API not initialized
                    LOG_WARNING("Attempt " + std::to_string(attempt + 1) +
                                ": Voicemeeter API not initialized. Response code: -2.");
                    break;
                case -3:  // Parameters dirty (e.g., configuration change)
                    LOG_DEBUG("Attempt " + std::to_string(attempt + 1) +
                              ": Parameters dirty, indicating possible config change. Response code: -3.");
                    break;
                case -4:  // General API error
                    LOG_ERROR("Attempt " + std::to_string(attempt + 1) +
                              ": General API error. Response code: -4.");
                    break;
                default:  // Unknown response code
                    LOG_ERROR("Attempt " + std::to_string(attempt + 1) +
                              ": Unknown response code received: " + std::to_string(responseCode) + ".");
                    break;
            }

            if (audioEngineRunning) break;  // Exit loop if engine confirmed running

            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }

        if (!audioEngineRunning) {
            LOG_ERROR("Voicemeeter audio engine check failed after " +
                      std::to_string(MAX_RETRIES) + " attempts. Engine not responsive.");
            Shutdown();
            return false;
        }



    LOG_DEBUG("Voicemeeter login and audio engine confirmation successful.");

    // Initialize channel states based on Voicemeeter type
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

    // Initialize Input Strips
    for (int i = 0; i < maxStrips; ++i) {
        if (!InitializeVoicemeeterState(i, VolumeUtils::ChannelType::Input)) {
            LOG_WARNING("Failed to initialize state for Input Strip " + std::to_string(i));
        }
    }

    // Initialize Output Buses
    for (int i = 0; i < maxBuses; ++i) {
        if (!InitializeVoicemeeterState(i, VolumeUtils::ChannelType::Output)) {
            LOG_WARNING("Failed to initialize state for Output Bus " + std::to_string(i));
        }
    }
}
else {
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
    UninitializeCOM();  // Ensure COM is uninitialized
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
bool VoicemeeterManager::GetVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float& volumePercent, bool& isMuted) {
    float gainValue = 0.0f;
    float muteValue = 0.0f;
    std::string gainParam;
    std::string muteParam;

    if (channelType == VolumeUtils::ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {  // VolumeUtils::ChannelType::Output
        gainParam = "Bus[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    if (VBVMR_GetParameterFloat(const_cast<char*>(gainParam.c_str()), &gainValue) != 0) {
        LOG_ERROR("Failed to get Gain parameter for " + gainParam);
        return false;
    }

    if (VBVMR_GetParameterFloat(const_cast<char*>(muteParam.c_str()), &muteValue) != 0) {
        LOG_ERROR("Failed to get Mute parameter for " + muteParam);
        return false;
    }

    volumePercent = VolumeUtils::dBmToPercent(gainValue);  // Use VolumeUtils
    isMuted = (muteValue != 0.0f);

    return true;
}

// Updates the Voicemeeter volume and mute state of a channel
void VoicemeeterManager::UpdateVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float volumePercent, bool isMuted) {
    float dBmValue = VolumeUtils::PercentToDbm(volumePercent);  // Use VolumeUtils

    std::string gainParam;
    std::string muteParam;

    if (channelType == VolumeUtils::ChannelType::Input) {
        gainParam = "Strip[" + std::to_string(channelIndex) + "].Gain";
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {  // VolumeUtils::ChannelType::Output
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
    if (result == 0) {
        LOG_DEBUG("IsParametersDirty: Parameters have changed (dirty).");
        return true;
    } else if (result == 1) {
        LOG_DEBUG("IsParametersDirty: Parameters have not changed (not dirty).");
        return false;
    } else {
        LOG_ERROR("IsParametersDirty: Unknown result code: " + std::to_string(result));
        return false;
    }
}

// Lists all monitorable audio devices
void VoicemeeterManager::ListMonitorableDevices() {
    // Ensure COM is initialized
    if (!comInitialized) {
        LOG_ERROR("COM library is not initialized. Unable to list monitorable devices.");
        return;
    }

    HRESULT hr;
    ComPtr<IMMDeviceEnumerator> pEnumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), &pEnumerator);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create MMDeviceEnumerator. HRESULT: " + std::to_string(hr));
        return;
    }

    ComPtr<IMMDeviceCollection> pCollection;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to enumerate audio endpoints. HRESULT: " + std::to_string(hr));
        return;
    }

    UINT count = 0;
    hr = pCollection->GetCount(&count);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get device count. HRESULT: " + std::to_string(hr));
        return;
    }

    LOG_INFO("Available audio devices for monitoring:");
    LOG_INFO("-------------------------------------------------");
    LOG_INFO("| Index | Device Name                     | UUID                     |");
    LOG_INFO("-------------------------------------------------");

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> pDevice;
        hr = pCollection->Item(i, &pDevice);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        // Get the device ID (UUID)
        LPWSTR deviceId = nullptr;
        hr = pDevice->GetId(&deviceId);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get device ID for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            continue;
        }

        // Get the device name
        ComPtr<IPropertyStore> pProps;
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to open property store for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            CoTaskMemFree(deviceId);
            continue;
        }

        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get friendly name for device at index " + std::to_string(i) + ". HRESULT: " + std::to_string(hr));
            PropVariantClear(&varName);
            CoTaskMemFree(deviceId);
            continue;
        }

        // Convert device ID (UUID) from wide string to UTF-8
        std::wstring wDeviceId(deviceId);
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wDeviceId.c_str(), -1, NULL, 0, NULL, NULL);
        std::string deviceUUID(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wDeviceId.c_str(), -1, &deviceUUID[0], size_needed, NULL, NULL);

        // Convert device name from wide string to UTF-8
        std::wstring wDeviceName(varName.pwszVal);
        size_needed = WideCharToMultiByte(CP_UTF8, 0, wDeviceName.c_str(), -1, NULL, 0, NULL, NULL);
        std::string deviceName(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wDeviceName.c_str(), -1, &deviceName[0], size_needed, NULL, NULL);

        LOG_INFO("| " + std::to_string(i) +
                 "     | " + deviceName +
                 " | " + deviceUUID + " |");

        // Clean up
        PropVariantClear(&varName);
        CoTaskMemFree(deviceId);
    }

    LOG_INFO("-------------------------------------------------");
}

// Initializes the state of a specific Voicemeeter channel
bool VoicemeeterManager::InitializeVoicemeeterState(int channelIndex, VolumeUtils::ChannelType channelType) {
    float volumePercent = 0.0f;
    bool isMuted = false;

    if (GetVoicemeeterVolume(channelIndex, channelType, volumePercent, isMuted)) {
        std::lock_guard<std::mutex> lock(channelStatesMutex);
        channelStates[channelIndex].volume = volumePercent;
        channelStates[channelIndex].isMuted = isMuted;
    } else {
        std::lock_guard<std::mutex> lock(channelStatesMutex);
        channelStates[channelIndex].volume = 0.0f;
        channelStates[channelIndex].isMuted = false;
    }

    LOG_DEBUG("Initial Voicemeeter Volume for Channel " + std::to_string(channelIndex) +
              ": " + std::to_string(channelStates[channelIndex].volume) +
              "% " + (channelStates[channelIndex].isMuted ? "(Muted)" : "(Unmuted)"));

    return true;  // Modify this to return false based on specific failure conditions if needed
}

// Retrieves the current volume percentage of a channel
float VoicemeeterManager::GetChannelVolume(int channelIndex, VolumeUtils::ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelStatesMutex);
    auto it = channelStates.find(channelIndex);
    if (it != channelStates.end()) {
        return it->second.volume;
    }
    return -1.0f;  // Indicates failure or uninitialized state
}

// Checks if a channel is muted
bool VoicemeeterManager::IsChannelMuted(int channelIndex, VolumeUtils::ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelStatesMutex);
    auto it = channelStates.find(channelIndex);
    if (it != channelStates.end()) {
        return it->second.isMuted;
    }
    return false;  // Default to unmuted if state is unavailable
}

bool VoicemeeterManager::SetMute(int channelIndex, VolumeUtils::ChannelType channelType, bool isMuted) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    bool result = SetMuteInternal(channelIndex, channelType, isMuted);
    if (result) {
        // Update the internal state
        std::lock_guard<std::mutex> stateLock(channelStatesMutex);
        channelStates[channelIndex].isMuted = isMuted;
        LOG_INFO("SetMute successful: Channel " + std::to_string(channelIndex) +
                 " (" + (channelType == VolumeUtils::ChannelType::Input ? "Input" : "Output") +
                 ") is now " + (isMuted ? "Muted" : "Unmuted") + ".");
    } else {
        LOG_ERROR("SetMute failed: Unable to set mute state for Channel " + std::to_string(channelIndex) + ".");
    }
    return result;
}

bool VoicemeeterManager::SetMuteInternal(int channelIndex, VolumeUtils::ChannelType channelType, bool isMuted) {
    std::string muteParam;

    if (channelType == VolumeUtils::ChannelType::Input) {
        muteParam = "Strip[" + std::to_string(channelIndex) + "].Mute";
    } else {  // Output
        muteParam = "Bus[" + std::to_string(channelIndex) + "].Mute";
    }

    float muteValue = isMuted ? 1.0f : 0.0f;
    long apiResult = VBVMR_SetParameterFloat(const_cast<char*>(muteParam.c_str()), muteValue);

    if (apiResult != 0) {
        LOG_ERROR("Failed to set mute parameter: " + muteParam + " with value: " + std::to_string(muteValue));
        return false;
    }

    return true;
}
