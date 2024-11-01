#include "ChannelUtility.h"
#include "Logger.h"

void ChannelUtility::ListInputs(VoicemeeterManager& vmrManager) {
    vmrManager.ListInputs();
}

void ChannelUtility::ListOutputs(VoicemeeterManager& vmrManager) {
    vmrManager.ListOutputs();
}

void ChannelUtility::ListAllChannels(VoicemeeterManager& vmrManager) {
    vmrManager.ListAllChannels();
}
