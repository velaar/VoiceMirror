#ifndef VOICEMEETERMANAGER_H
#define VOICEMEETERMANAGER_H

#include "VoicemeeterAPI.h" // Ensure this is included before any usage
#include <mutex>

/**
 * @brief The VoicemeeterManager class encapsulates interactions with the Voicemeeter API.
 *
 * This class handles initialization, shutdown, and provides utility functions to interact
 * with Voicemeeter. It ensures that the API is properly managed and provides thread safety.
 */
class VoicemeeterManager
{
public:
    /**
     * @brief Constructor for VoicemeeterManager.
     *
     * Initializes member variables but does not perform any initialization of the Voicemeeter API.
     */
    VoicemeeterManager();

    /**
     * @brief Initializes the Voicemeeter API and logs in.
     *
     * Attempts to initialize the Voicemeeter API and logs in. If the login fails, it attempts to
     * run Voicemeeter and retries the login after a short delay.
     *
     * @param voicemeeterType The type of Voicemeeter to initialize (1: Voicemeeter, 2: Banana, 3: Potato).
     * @return True if initialization and login were successful, false otherwise.
     */
    bool Initialize(int voicemeeterType);

    /**
     * @brief Shuts down the Voicemeeter API and logs out.
     *
     * If logged in, it logs out and shuts down the API.
     */
    void Shutdown();

    /**
     * @brief Sends a shutdown command to Voicemeeter.
     *
     * Instructs Voicemeeter to shut down gracefully by setting the "Command.Shutdown" parameter.
     */
    void ShutdownCommand();

    /**
     * @brief Restarts the Voicemeeter audio engine.
     *
     * This function locks the toggle mutex to ensure thread safety, delays for a specified duration
     * before and after restarting the audio engine, and logs the process.
     *
     * @param beforeRestartDelay Delay before restarting, in seconds (default is 2 seconds).
     * @param afterRestartDelay Delay after restarting, in seconds (default is 2 seconds).
     */
    void RestartAudioEngine(int beforeRestartDelay = 2, int afterRestartDelay = 2);

    /**
     * @brief Sets the debug mode for VoicemeeterManager.
     *
     * Enables or disables debug mode, which controls the verbosity of logging.
     *
     * @param newDebugMode True to enable debug mode, false to disable.
     */
    void SetDebugMode(bool newDebugMode);

    /**
     * @brief Gets the current debug mode state.
     *
     * @return True if debug mode is enabled, false otherwise.
     */
    bool GetDebugMode();

    /**
     * @brief Gets a reference to the VoicemeeterAPI instance.
     *
     * @return Reference to VoicemeeterAPI.
     */
    VoicemeeterAPI& GetAPI();

    /**
     * @brief Lists all Voicemeeter channels with labels.
     *
     * This function retrieves and logs all input strips and output buses, including their labels.
     */
    void ListAllChannels();

    /**
     * @brief Lists Voicemeeter virtual inputs.
     *
     * This function retrieves and logs all available Voicemeeter virtual input channels.
     */
    void ListInputs();

    /**
     * @brief Lists Voicemeeter virtual outputs.
     *
     * This function retrieves and logs all available Voicemeeter virtual output buses.
     */
    void ListOutputs();

private:
    VoicemeeterAPI vmrAPI;          ///< Instance of VoicemeeterAPI to interact with Voicemeeter.
    bool loggedIn;                  ///< Flag indicating if logged into Voicemeeter API.
    bool debugMode;                 ///< Debug mode flag.
    std::mutex toggleMutex;         ///< Mutex for thread safety in toggle operations.
};

#endif // VOICEMEETERMANAGER_H
