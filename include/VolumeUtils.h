#ifndef VOLUMEUTILS_H
#define VOLUMEUTILS_H

#include <cmath>
#include <string>
#include <algorithm> // For std::clamp
#include "Defconf.h" // To access default values

namespace VolumeUtils {

    // Trim function for configs
    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    // Converts a scalar (0.0 - 1.0) to a percentage (0 - 100), clamped to range
    inline float ScalarToPercent(float scalar) { 
        return std::clamp(scalar, 0.0f, 1.0f) * 100.0f; 
    }

    // Converts a percentage (0 - 100) to a scalar (0.0 - 1.0), clamped to range
    inline float PercentToScalar(float percent) { 
        return std::clamp(percent, 0.0f, 100.0f) / 100.0f; 
    }

    // Converts dBm to percentage (0 - 100), with optional min and max dBm defaults
    inline float dBmToPercent(float dBm, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) { 
        dBm = std::clamp(dBm, minDbm, maxDbm);
        return ((dBm - minDbm) / (maxDbm - minDbm)) * 100.0f;
    }

    // Converts percentage (0 - 100) to dBm, with optional min and max dBm defaults
    inline float PercentToDbm(float percent, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) { 
        percent = std::clamp(percent, 0.0f, 100.0f);
        return (percent / 100.0f) * (maxDbm - minDbm) + minDbm;
    }

    // Checks if two floats are equal within a small epsilon
    inline bool IsFloatEqual(float a, float b, float epsilon = 0.001f) {
        return std::fabs(a - b) < epsilon;
    }

    // Define ChannelType enum centrally
    enum class ChannelType {
        Input,
        Output
    };

}

#endif // VOLUMEUTILS_H
