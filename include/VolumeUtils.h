#ifndef VOLUMEUTILS_H
#define VOLUMEUTILS_H

#include <algorithm>  // For std::clamp
#include <cmath>
#include <string>

#include "Defconf.h"  // To access default values
#include "Logger.h"   // Assuming Logger.h is needed globally

namespace VolumeUtils {

// Utility functions only
inline float ScalarToPercent(float scalar) {
    return std::clamp(scalar, 0.0f, 1.0f) * 100.0f;
}

inline float PercentToScalar(float percent) {
    return std::clamp(percent, 0.0f, 100.0f) / 100.0f;
}

inline float dBmToPercent(float dBm, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) {
    dBm = std::clamp(dBm, minDbm, maxDbm);
    return ((dBm - minDbm) / (maxDbm - minDbm)) * 100.0f;
}

inline float PercentToDbm(float percent, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) {
    percent = std::clamp(percent, 0.0f, 100.0f);
    return (percent / 100.0f) * (maxDbm - minDbm) + minDbm;
}

inline bool IsFloatEqual(float a, float b, float epsilon = 1.0f) { // Adjusted epsilon for full percent
    return std::fabs(a - b) < epsilon;
}

}  // namespace VolumeUtils

#endif  // VOLUMEUTILS_H
