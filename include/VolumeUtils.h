#ifndef VOLUMEUTILS_H
#define VOLUMEUTILS_H

#include <cmath>

namespace VolumeUtils {
    // Converts a scalar (0.0 - 1.0) to a percentage (0 - 100)
    inline float ScalarToPercent(float scalar) { 
        return scalar * 100.0f; 
    }

    // Converts a percentage (0 - 100) to a scalar (0.0 - 1.0)
    inline float PercentToScalar(float percent) { 
        return percent / 100.0f; 
    }

    // Converts dBm to percentage
    inline float dBmToPercent(float dBm) { 
        return (dBm + 60.0f) * (100.0f / 60.0f); 
    }

    // Converts percentage to dBm
    inline float PercentToDbm(float percent) { 
        return (percent * (60.0f / 100.0f)) - 60.0f;
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
