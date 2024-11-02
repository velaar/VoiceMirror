// VoicemeeterManager.cpp
#include "VoicemeeterManager.h"
#include "VolumeUtils.h"
#include "Logger.h"

#include <thread>
#include <chrono>
#include <string>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <mmdeviceapi.h>  // For IMMDeviceEnumerator and related audio APIs
#include <Functiondiscoverykeys_devpkey.h>  // For PKEY_Device_FriendlyName


#include <wrl/client.h>               // Smart pointers for COM interfaces

using Microsoft::WRL::ComPtr;

/**
 * @brief Constructor for VoicemeeterManager.
 *
 * Initializes member variables but does not perform any initialization of the
 * Voicemeeter API.
 */
VoicemeeterManager::VoicemeeterManager() 
    : loggedIn(false), debugMode(false), comInitialized(false) {}

/**
 * @brief Destructor for VoicemeeterManager.
 *
 * Ensures that COM is uninitialized and the Voicemeeter API is shut down.
 */
VoicemeeterManager::~VoicemeeterManager() {
    Shutdown(); // Ensure proper shutdown and COM uninitialization
}

/**
 * @brief Initializes the COM library for use by the calling thread.
 *
 * This method ensures that COM is initialized before interacting with the Voicemeeter API.
 *
 * @return True if COM was successfully initialized or already initialized, false otherwise.
 */
bool VoicemeeterManager::InitializeCOM() {
    if (!comInitialized) {
        HRESULT hr = CoInitializeEx(
            nullptr, 
            COINIT_APARTMENTTHREADED // Use apartment-threaded model
        );
        if (hr == RPC_E_CHANGED_MODE) {
            LOG_WARNING("COM library already initialized with a different threading model.");
            // Depending on application requirements, handle accordingly.
            // For now, consider COM as initialized.
            comInitialized = true;
            return true;
        } else if (FAILED(hr)) {
            LOG_ERROR("Failed to initialize COM library. HRESULT: " +
                       std::to_string(hr));
            return false;
        }
        comInitialized = true;
        LOG_DEBUG("COM library initialized successfully.");
    }
    return true;
}

/**
 * @brief Uninitializes the COM library on the current thread.
 *
 * This method ensures that COM is properly uninitialized when done.
 */
void VoicemeeterManager::UninitializeCOM() {
    if (comInitialized) {
        CoUninitialize();
        comInitialized = false;
        LOG_DEBUG("COM library uninitialized.");
    }
}

/**
 * @brief Initializes the Voicemeeter API and logs in.
 *
 * Attempts to initialize the Voicemeeter API and logs in. If the login fails,
 * it attempts to run Voicemeeter and retries the login after a short delay.
 * Additionally, confirms that the Voicemeeter audio engine is running
 * before finalizing the initialization.
 *
 * @param voicemeeterType The type of Voicemeeter to initialize (1: Voicemeeter,
 * 2: Banana, 3: Potato).
 * @return True if initialization, login, and audio engine confirmation were successful, false otherwise.
 */
bool VoicemeeterManager::Initialize(int voicemeeterType) {
    if (loggedIn) {
        LOG_DEBUG("Voicemeeter is already logged in.");
        return true;
    }

    LOG_DEBUG("Initializing Voicemeeter Manager...");

    if (!InitializeCOM()) { // Initialize COM before using Voicemeeter API
        LOG_ERROR("COM initialization failed.");
        return false;
    }

    if (!vmrAPI.Initialize()) {
        LOG_ERROR("Failed to initialize Voicemeeter API.");
        UninitializeCOM(); // Clean up COM if API initialization fails
        return false;
    }

    long loginResult = vmrAPI.Login();
    LOG_DEBUG("Voicemeeter login result: " + std::to_string(loginResult));
    loggedIn = (loginResult == 0 || loginResult == 1);

    long vmType = 0;
    if (loggedIn) {
        HRESULT hr = vmrAPI.GetVoicemeeterType(&vmType);
        LOG_DEBUG("GetVoicemeeterType result: " + std::to_string(hr) +
                                   ", Type: " + std::to_string(vmType));
        if (hr != 0) {
            LOG_WARNING("Voicemeeter is not running. Attempting to start it.");
            loggedIn = false;
        }
    }

    if (!loggedIn) {
        LOG_WARNING("Voicemeeter login failed, attempting to run Voicemeeter Type:" +
                std::to_string(voicemeeterType));
        long runResult = vmrAPI.RunVoicemeeter(voicemeeterType);
        LOG_DEBUG("RunVoicemeeter result: " + std::to_string(runResult));

        if (runResult != 0) {
            LOG_ERROR("Failed to run Voicemeeter. Error code: " +
                                   std::to_string(runResult));
            UninitializeCOM(); // Clean up COM if running Voicemeeter fails
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        loginResult = vmrAPI.Login();
        LOG_DEBUG("Voicemeeter login result after running: " +
                                   std::to_string(loginResult));
        loggedIn = (loginResult == -2 || loginResult == 0);

        // Check again if Voicemeeter is running
        if (loggedIn) {
            HRESULT hr = vmrAPI.GetVoicemeeterType(&vmType);
            LOG_DEBUG("GetVoicemeeterType result after running: " + std::to_string(hr) +
                    ", Type: " + std::to_string(vmType));
            if (hr != 0) {
                LOG_ERROR("Failed to start Voicemeeter.");
                loggedIn = false;
            }
        }
    }

    if (loggedIn) {
        // Confirm that the Voicemeeter audio engine is running
        bool audioEngineRunning = false;
        const int maxRetries = 10;
        const int retryDelayMs = 500; // 0.5 seconds
        for (int attempt = 0; attempt < maxRetries; ++attempt) {
            float engineStatus = 0.0f;
            if (vmrAPI.GetParameterFloat("Application.AudioEngineRunning", &engineStatus) == 0) {
                if (engineStatus > 0.5f) { // Assuming a value >0.5 indicates running
                    audioEngineRunning = true;
                    LOG_DEBUG("Voicemeeter audio engine is running.");
                    break;
                }
            }
            LOG_DEBUG("Waiting for Voicemeeter audio engine to start...");
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
        }

        if (!audioEngineRunning) {
            LOG_ERROR("Voicemeeter audio engine is not running after login.");
            Shutdown(); // Clean up if audio engine is not running
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

    } else {
        LOG_ERROR("Voicemeeter login failed.");
        UninitializeCOM(); // Clean up COM if login fails
    }

    return loggedIn;
}

/**
 * @brief Shuts down the Voicemeeter API and logs out.
 *
 * If logged in, it logs out and shuts down the API.
 */
void VoicemeeterManager::Shutdown() {
    if (loggedIn) {
        LOG_DEBUG("Shutting down Voicemeeter.");
        vmrAPI.Logout();
        vmrAPI.Shutdown();
        loggedIn = false;
    }
    UninitializeCOM(); // Ensure COM is uninitialized
}

/**
 * @brief Sends a shutdown command to Voicemeeter.
 *
 * Instructs Voicemeeter to shut down gracefully by setting the
 * "Command.Shutdown" parameter.
 */
void VoicemeeterManager::ShutdownCommand() {
    LOG_DEBUG("Sending shutdown command to Voicemeeter.");
    vmrAPI.SetParameterFloat("Command.Shutdown", 1);
}

/**
 * @brief Restarts the Voicemeeter audio engine.
 *
 * This function locks the toggle mutex to ensure thread safety, delays for a
 * specified duration before and after restarting the audio engine, and logs the
 * process.
 *
 * @param beforeRestartDelay Delay before restarting, in seconds (default is 2
 * seconds).
 * @param afterRestartDelay Delay after restarting, in seconds (default is 2
 * seconds).
 */
void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay,
                                            int afterRestartDelay) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    LOG_DEBUG("Restarting Voicemeeter audio engine...");

    // Delay before restarting
    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    vmrAPI.SetParameterFloat("Command.Restart", 1);

    // Delay after restarting
    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));

    LOG_DEBUG("Voicemeeter audio engine restarted.");
}

/**
 * @brief Sets the debug mode for VoicemeeterManager.
 *
 * Enables or disables debug mode, which controls the verbosity of logging.
 *
 * @param newDebugMode True to enable debug mode, false to disable.
 */
void VoicemeeterManager::SetDebugMode(bool newDebugMode) {
    this->debugMode = newDebugMode;
}

/**
 * @brief Gets the current debug mode state.
 *
 * @return True if debug mode is enabled, false otherwise.
 */
bool VoicemeeterManager::GetDebugMode() { // Removed 'const'
    return debugMode;
}

/**
 * @brief Gets a reference to the VoicemeeterAPI instance.
 *
 * @return Reference to VoicemeeterAPI.
 */
VoicemeeterAPI& VoicemeeterManager::GetAPI() { 
    return vmrAPI; 
}

/**
 * @brief Checks if any Voicemeeter parameters have changed (are dirty).
 *
 * This method queries the Voicemeeter API to determine if any parameters have been modified
 * since the last check. It logs the result for debugging purposes.
 *
 * @return True if parameters are dirty (have changed), false otherwise.
 */
bool VoicemeeterManager::IsParametersDirty() {
    if (!loggedIn) {
        LOG_ERROR("Cannot check parameters dirty state: not logged in.");
        return false;
    }

    long result = vmrAPI.IsParametersDirty();
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

/**
 * @brief Lists all Voicemeeter channels with labels.
 *
 * This function retrieves and logs all input strips and output buses, including
 * their labels.
 */
void VoicemeeterManager::ListAllChannels() { // Removed 'const'
    long voicemeeterType = 0;
    HRESULT hr = vmrAPI.GetVoicemeeterType(&voicemeeterType);
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
      auto PrintParameter = [&](const std::string& param, const std::string& type,
                                int index) {
          char label[512] = {0};
          long result = vmrAPI.GetParameterStringA(param.c_str(), label);
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

/**
 * @brief Lists Voicemeeter virtual inputs.
 *
 * This function retrieves and logs all available Voicemeeter virtual input
 * channels.
 */
void VoicemeeterManager::ListInputs() { // Removed 'const'
    try {
        int maxStrips = 8;  // Maximum number of strips to check
        LOG_INFO("Available Voicemeeter Virtual Inputs:");

        for (int i = 0; i < maxStrips; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0) {
                LOG_INFO(std::to_string(i) + ": " + label);
            } else {
                break;  // No more strips
            }
        }
    } catch (const std::exception &ex) {
        LOG_ERROR("Error listing Voicemeeter inputs: " + std::string(ex.what()));
    }
}

/**
 * @brief Lists Voicemeeter virtual outputs.
 *
 * This function retrieves and logs all available Voicemeeter virtual output
 * buses.
 */
void VoicemeeterManager::ListOutputs() { // Removed 'const'
    try {
        int maxBuses = 8;  // Maximum number of buses to check
        LOG_INFO("Available Voicemeeter Virtual Outputs:");

        for (int i = 0; i < maxBuses; ++i) {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0) {
                LOG_INFO(std::to_string(i) + ": " + label);
            } else {
                break;  // No more buses
            }
        }
    } catch (const std::exception &ex) {
        LOG_ERROR("Error listing Voicemeeter outputs: " + std::string(ex.what()));
    }
}

/**
 * @brief Gets the current Voicemeeter volume and mute state
 *
 * @param channelIndex Index of the channel
 * @param channelType Type of the channel (Input or Output)
 * @param volumePercent Reference to store the volume percentage
 * @param isMuted Reference to store the mute state
 * @return True if successful, false otherwise
 */
bool VoicemeeterManager::GetVoicemeeterVolume(int channelIndex, VolumeUtils::ChannelType channelType, float &volumePercent, bool &isMuted) {
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

    if (vmrAPI.GetParameterFloat(gainParam.c_str(), &gainValue) != 0) {
        LOG_ERROR("Failed to get Gain parameter for " + gainParam);
        return false;
    }

    if (vmrAPI.GetParameterFloat(muteParam.c_str(), &muteValue) != 0) {
        LOG_ERROR("Failed to get Mute parameter for " + muteParam);
        return false;
    }

    volumePercent = VolumeUtils::dBmToPercent(gainValue);  // Use VolumeUtils
    isMuted = (muteValue != 0.0f);

    return true;
}

/**
 * @brief Updates the Voicemeeter volume and mute state
 *
 * @param channelIndex Index of the channel
 * @param channelType Type of the channel (Input or Output)
 * @param volumePercent Volume percentage to set
 * @param isMuted Mute state to set
 */
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

    if (vmrAPI.SetParameterFloat(gainParam.c_str(), dBmValue) != 0) {
        LOG_ERROR("Failed to set Gain parameter for " + gainParam);
    }

    if (vmrAPI.SetParameterFloat(muteParam.c_str(), isMuted ? 1.0f : 0.0f) != 0) {
        LOG_ERROR("Failed to set Mute parameter for " + muteParam);
    }
    
    LOG_DEBUG("Voicemeeter volume updated: " + std::to_string(volumePercent) + "% " +
            (isMuted ? "(Muted)" : "(Unmuted)"));
}

/**
 * @brief Lists all monitorable audio devices by enumerating active render endpoints.
 *
 * This method uses the MMDevice API to enumerate all active render (output) audio
 * devices, retrieves their friendly names and UUIDs, and logs the information.
 */
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

/**
 * @brief Initializes the state of a specific Voicemeeter channel.
 *
 * Retrieves the current volume and mute state for the specified channel
 * and stores it internally.
 *
 * @param channelIndex Index of the channel.
 * @param channelType Type of the channel (Input or Output).
 * @return True if initialization was successful, false otherwise.
 */
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

    return true; // You can modify this to return false based on specific failure conditions
}

/**
 * @brief Retrieves the current volume percentage of a channel.
 *
 * @param channelIndex Index of the channel.
 * @param channelType Type of the channel (Input or Output).
 * @return Volume percentage if successful, -1.0f otherwise.
 */
float VoicemeeterManager::GetChannelVolume(int channelIndex, VolumeUtils::ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelStatesMutex);
    auto it = channelStates.find(channelIndex);
    if (it != channelStates.end()) {
        return it->second.volume;
    }
    return -1.0f; // Indicates failure or uninitialized state
}

/**
 * @brief Checks if a channel is muted.
 *
 * @param channelIndex Index of the channel.
 * @param channelType Type of the channel (Input or Output).
 * @return True if muted, false otherwise.
 */
bool VoicemeeterManager::IsChannelMuted(int channelIndex, VolumeUtils::ChannelType channelType) {
    std::lock_guard<std::mutex> lock(channelStatesMutex);
    auto it = channelStates.find(channelIndex);
    if (it != channelStates.end()) {
        return it->second.isMuted;
    }
    return false; // Default to unmuted if state is unavailable
}
