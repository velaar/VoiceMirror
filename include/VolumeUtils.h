// VolumeUtils.h

#pragma once

#include <Windows.h>
#include <algorithm>  
#include <cmath>
#include <string>

#include "Defconf.h" 
#include "Logger.h"

namespace VolumeUtils {

// Converts scalar (0.0 to 1.0) to percent (0.00 to 100.00), rounded to nearest 0.01%
inline float ScalarToPercent(float scalar) {
    float percent = std::clamp(scalar, 0.0f, 1.0f) * 100.0f;
    percent = std::round(percent * 100.0f) / 100.0f;  // Round to nearest 0.01%
    return percent;
}

// Converts percent (0.00 to 100.00) to scalar (0.0 to 1.0)
inline float PercentToScalar(float percent) {
    percent = std::clamp(percent, 0.0f, 100.0f);
    return percent / 100.0f;
}

// Converts dBm to percent (0.00 to 100.00), rounded to nearest 0.01%
inline float dBmToPercent(float dBm, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) {
    dBm = std::clamp(dBm, minDbm, maxDbm);
    float percent = ((dBm - minDbm) / (maxDbm - minDbm)) * 100.0f;
    percent = std::round(percent * 100.0f) / 100.0f;  // Round to nearest 0.01%
    return percent;
}

// Converts percent (0.00 to 100.00) to dBm, rounded to nearest 0.01 dBm
inline float PercentToDbm(float percent, float minDbm = DEFAULT_MIN_DBM, float maxDbm = DEFAULT_MAX_DBM) {
    percent = std::clamp(percent, 0.0f, 100.0f);
    float dBm = (percent / 100.0f) * (maxDbm - minDbm) + minDbm;
    dBm = std::round(dBm * 100.0f) / 100.0f;  // Round to nearest 0.01 dBm
    return dBm;
}

// Compares two floats for equality within specified decimal precision
inline bool IsFloatEqual(float a, float b, int decimalPlaces = 2) {
    auto factor = std::pow(10.0f, decimalPlaces);
    return std::round(a * factor) == std::round(b * factor);}

inline std::wstring ConvertToWString(const char* str) {
    if (!str) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring wstr(size_needed - 1, L'\0');  // Allocate without the null terminator
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], size_needed);
    return wstr;
}

inline std::string ConvertWStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &str[0], size_needed, nullptr, nullptr);
    return str;
}

inline std::wstring ConvertToWString(const wchar_t* wstr) {
    return std::wstring(wstr);
}

}  // namespace VolumeUtils
