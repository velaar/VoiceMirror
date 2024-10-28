// VoicemeeterManager.cpp
#include "VoicemeeterManager.h"

VoicemeeterManager::VoicemeeterManager() : loggedIn(false), debugMode(false) {}

bool VoicemeeterManager::Initialize(int voicemeeterType) {
    if (loggedIn) {
        DebugMessage("Voicemeeter is already logged in.");
        return true;
    }

    if (!vmrAPI.Initialize()) {
        DebugMessage("Failed to initialize Voicemeeter API.");
        return false;
    }

    long loginResult = vmrAPI.Login();
    loggedIn = (loginResult == 0 || loginResult == 1);
    
    if (loginResult != 0) {
        DebugMessage("Voicemeeter login failed, attempting to run Voicemeeter.");
        vmrAPI.RunVoicemeeter(voicemeeterType);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        loginResult = vmrAPI.Login();
        loggedIn = (loginResult == -2);
    }


    DebugMessage(loggedIn ? "Voicemeeter login successful." : "Voicemeeter login failed.");
    return loggedIn;
}

void VoicemeeterManager::Shutdown() {
    if (loggedIn) {
        DebugMessage("Shutting down Voicemeeter.");
        vmrAPI.Logout();
        vmrAPI.Shutdown();
        loggedIn = false;
    }
}

void VoicemeeterManager::ShutdownCommand() {
    DebugMessage("Sending shutdown command to Voicemeeter.");
    vmrAPI.SetParameterFloat("Command.Shutdown", 1);
}

void VoicemeeterManager::RestartAudioEngine(int beforeRestartDelay, int afterRestartDelay) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    DebugMessage("Restarting Voicemeeter audio engine...");

    // Delay before restarting
    std::this_thread::sleep_for(std::chrono::seconds(beforeRestartDelay));
    vmrAPI.SetParameterFloat("Command.Restart", 1);

    // Delay after restarting
    std::this_thread::sleep_for(std::chrono::seconds(afterRestartDelay));

    DebugMessage("Voicemeeter audio engine restarted.");
}

void VoicemeeterManager::SetDebugMode(bool newDebugMode) {
    this->debugMode = newDebugMode;
}

bool VoicemeeterManager::GetDebugMode() {
    return debugMode;
}

VoicemeeterAPI& VoicemeeterManager::GetAPI() {
    return vmrAPI;
}

void VoicemeeterManager::DebugMessage(const std::string& message) {
    if (debugMode) {
        std::cout << "[VMM] " << message << std::endl;
    }
}
