// VoicemeeterManager.cpp
#include "VoicemeeterManager.h"
#include "Logger.h"
#include <chrono>
#include <thread>

/**
 * @brief Constructor for VoicemeeterManager.
 *
 * Initializes member variables but does not perform any initialization of the Voicemeeter API.
 */
VoicemeeterManager::VoicemeeterManager() : loggedIn(false), debugMode(false) {}

/**
 * @brief Initializes the Voicemeeter API and logs in.
 *
 * Attempts to initialize the Voicemeeter API and logs in. If the login fails, it attempts to
 * run Voicemeeter and retries the login after a short delay.
 *
 * @param voicemeeterType The type of Voicemeeter to initialize (1: Voicemeeter, 2: Banana, 3: Potato).
 * @return True if initialization and login were successful, false otherwise.
 */
bool VoicemeeterManager::Initialize(int voicemeeterType)
{
    if (loggedIn)
    {
        Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter is already logged in.");
        return true;
    }

    Logger::Instance().Log(LogLevel::DEBUG, "Initializing Voicemeeter Manager...");

    if (!vmrAPI.Initialize())
    {
        Logger::Instance().Log(LogLevel::ERR, "Failed to initialize Voicemeeter API.");
        return false;
    }

    long loginResult = vmrAPI.Login();
    Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter login result: " + std::to_string(loginResult));
    loggedIn = (loginResult == 0 || loginResult == 1);

    long vmType = 0;
    if (loggedIn)
    {
        HRESULT hr = vmrAPI.GetVoicemeeterType(&vmType);
        Logger::Instance().Log(LogLevel::DEBUG, "GetVoicemeeterType result: " + std::to_string(hr) + ", Type: " + std::to_string(vmType));
        if (hr != 0)
        {
            Logger::Instance().Log(LogLevel::WARNING, "Voicemeeter is not running. Attempting to start it.");
            loggedIn = false;
        }
    }

      if (!loggedIn)
    {
        Logger::Instance().Log(LogLevel::WARNING, "Voicemeeter login failed, attempting to run Voicemeeter Type:" + std::to_string(voicemeeterType));
        long runResult = vmrAPI.RunVoicemeeter(voicemeeterType);
        Logger::Instance().Log(LogLevel::DEBUG, "RunVoicemeeter result: " + std::to_string(runResult));

        if (runResult != 0)
        {
            Logger::Instance().Log(LogLevel::ERR, "Failed to run Voicemeeter. Error code: " + std::to_string(runResult));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        loginResult = vmrAPI.Login();
        Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter login result after running: " + std::to_string(loginResult));
        loggedIn = (loginResult == -2 || loginResult == 0);

        // Check again if Voicemeeter is running
        if (loggedIn)
        {
            HRESULT hr = vmrAPI.GetVoicemeeterType(&vmType);
            Logger::Instance().Log(LogLevel::DEBUG, "GetVoicemeeterType result after running: " + std::to_string(hr) + ", Type: " + std::to_string(vmType));
            if (hr != 0)
            {
                Logger::Instance().Log(LogLevel::ERR, "Failed to start Voicemeeter.");
                loggedIn = false;
            }
        }
    }

    if (loggedIn)
    {
        Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter login successful.");
    }
    else
    {
        Logger::Instance().Log(LogLevel::ERR, "Voicemeeter login failed.");
    }

    return loggedIn;
}

/**
 * @brief Shuts down the Voicemeeter API and logs out.
 *
 * If logged in, it logs out and shuts down the API.
 */
void VoicemeeterManager::Shutdown()
{
    if (loggedIn)
    {
        Logger::Instance().Log(LogLevel::DEBUG, "Shutting down Voicemeeter.");
        vmrAPI.Logout();
        vmrAPI.Shutdown();
        loggedIn = false;
    }
}

/**
 * @brief Sends a shutdown command to Voicemeeter.
 *
 * Instructs Voicemeeter to shut down gracefully by setting the "Command.Shutdown" parameter.
 */
void VoicemeeterManager::ShutdownCommand()
{
    Logger::Instance().Log(LogLevel::DEBUG, "Sending shutdown command to Voicemeeter.");
    vmrAPI.SetParameterFloat("Command.Shutdown", 1);
}

/**
 * @brief Restarts the Voicemeeter audio engine.
 *
 * This function locks the toggle mutex to ensure thread safety, delays for a specified duration
 * before and after restarting the audio engine, and logs the process.
 *
 * @param beforeRestartDelay Delay before restarting, in seconds (default is 2 seconds).
 * @param afterRestartDelay Delay after restarting, in seconds (default is 2 seconds).
 */
void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay)
{
    std::lock_guard<std::mutex> lock(toggleMutex);
    Logger::Instance().Log(LogLevel::DEBUG, "Restarting Voicemeeter audio engine...");

    // Delay before restarting
    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    vmrAPI.SetParameterFloat("Command.Restart", 1);

    // Delay after restarting
    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));

    Logger::Instance().Log(LogLevel::DEBUG, "Voicemeeter audio engine restarted.");
}

/**
 * @brief Sets the debug mode for VoicemeeterManager.
 *
 * Enables or disables debug mode, which controls the verbosity of logging.
 *
 * @param newDebugMode True to enable debug mode, false to disable.
 */
void VoicemeeterManager::SetDebugMode(bool newDebugMode)
{
    this->debugMode = newDebugMode;
}

/**
 * @brief Gets the current debug mode state.
 *
 * @return True if debug mode is enabled, false otherwise.
 */
bool VoicemeeterManager::GetDebugMode()
{
    return debugMode;
}

/**
 * @brief Gets a reference to the VoicemeeterAPI instance.
 *
 * @return Reference to VoicemeeterAPI.
 */
VoicemeeterAPI &VoicemeeterManager::GetAPI()
{
    return vmrAPI;
}

/**
 * @brief Lists all Voicemeeter channels with labels.
 *
 * This function retrieves and logs all input strips and output buses, including their labels.
 */
void VoicemeeterManager::ListAllChannels()
{
    long voicemeeterType = 0;
    HRESULT hr = vmrAPI.GetVoicemeeterType(&voicemeeterType);
    if (hr != 0)
    {
        Logger::Instance().Log(LogLevel::ERR, "Failed to get Voicemeeter type.");
        return;
    }

    std::string typeStr;
    int maxStrips = 0;
    int maxBuses = 0;

    // Set limits based on Voicemeeter type
    switch (voicemeeterType)
    {
    case 1:
        typeStr = "Voicemeeter";
        maxStrips = 3; // Including virtual inputs
        maxBuses = 2;
        break;
    case 2:
        typeStr = "Voicemeeter Banana";
        maxStrips = 5; // Including virtual inputs
        maxBuses = 5;
        break;
    case 3:
        typeStr = "Voicemeeter Potato";
        maxStrips = 8; // Including virtual inputs
        maxBuses = 8;
        break;
    default:
        Logger::Instance().Log(LogLevel::ERR, "Unknown Voicemeeter type.");
        return;
    }

    Logger::Instance().Log(LogLevel::INFO, "Voicemeeter Type: " + typeStr);

    // Lambda to print channel parameters
    auto PrintParameter = [&](const std::string &param, const std::string &type, int index)
    {
        char label[512] = {0};
        long result = vmrAPI.GetParameterStringA(param.c_str(), label);
        if (result == 0 && strlen(label) > 0 && strlen(label) < sizeof(label))
        {
            Logger::Instance().Log(LogLevel::INFO, "| " + std::to_string(index) + " | " + label + " | " + type + " |");
        }
        else
        {
            Logger::Instance().Log(LogLevel::INFO, "| " + std::to_string(index) + " | N/A | " + type + " |");
        }
    };

    // Print Strips
    Logger::Instance().Log(LogLevel::INFO, "\nStrips:");
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");
    Logger::Instance().Log(LogLevel::INFO, "| Index   | Label                | Type         |");
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");

    for (int i = 0; i < maxStrips; ++i)
    {
        std::string paramName = "Strip[" + std::to_string(i) + "].Label";
        PrintParameter(paramName, "Input Strip", i);
    }
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");

    // Print Buses
    Logger::Instance().Log(LogLevel::INFO, "\nBuses:");
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");
    Logger::Instance().Log(LogLevel::INFO, "| Index   | Label                | Type         |");
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");

    for (int i = 0; i < maxBuses; ++i)
    {
        std::string paramNameBus = "Bus[" + std::to_string(i) + "].Label";
        std::string busType = "BUS " + std::to_string(i);
        PrintParameter(paramNameBus, busType, i);
    }
    Logger::Instance().Log(LogLevel::INFO, "+---------+----------------------+--------------+");
}

/**
 * @brief Lists Voicemeeter virtual inputs.
 *
 * This function retrieves and logs all available Voicemeeter virtual input channels.
 */
void VoicemeeterManager::ListInputs()
{
    try
    {
        int maxStrips = 8; // Maximum number of strips to check
        Logger::Instance().Log(LogLevel::INFO, "Available Voicemeeter Virtual Inputs:");

        for (int i = 0; i < maxStrips; ++i)
        {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0)
            {
                Logger::Instance().Log(LogLevel::INFO, std::to_string(i) + ": " + label);
            }
            else
            {
                break; // No more strips
            }
        }
    }
    catch (const std::exception &ex)
    {
        Logger::Instance().Log(LogLevel::ERR, "Error listing Voicemeeter inputs: " + std::string(ex.what()));
    }
}

/**
 * @brief Lists Voicemeeter virtual outputs.
 *
 * This function retrieves and logs all available Voicemeeter virtual output buses.
 */
void VoicemeeterManager::ListOutputs()
{
    try
    {
        int maxBuses = 8; // Maximum number of buses to check
        Logger::Instance().Log(LogLevel::INFO, "Available Voicemeeter Virtual Outputs:");

        for (int i = 0; i < maxBuses; ++i)
        {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0)
            {
                Logger::Instance().Log(LogLevel::INFO, std::to_string(i) + ": " + label);
            }
            else
            {
                break; // No more buses
            }
        }
    }
    catch (const std::exception &ex)
    {
        Logger::Instance().Log(LogLevel::ERR, "Error listing Voicemeeter outputs: " + std::string(ex.what()));
    }
}
