#pragma once
#include <string_view>
#include "VoicemeeterManager.h"

class ChannelUtility {
public:
    static void ListInputs(VoicemeeterManager& vmrManager);
    static void ListOutputs(VoicemeeterManager& vmrManager);
    static void ListAllChannels(VoicemeeterManager& vmrManager);
};
