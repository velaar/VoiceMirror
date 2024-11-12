#pragma once

#include <Windows.h>

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

inline bool IsFloatEqual(float a, float b, float epsilon = 1.0f) {  // Adjusted epsilon for full percent
    return std::fabs(a - b) < epsilon;
}

}  

inline std::wstring ConvertToWString(const char* str) {
    if (!str) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring wstr(size_needed - 1, L'\0');  // Allocate without the null terminator
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], size_needed);

    return wstr;
}

inline std::string ConvertWStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);

    return str;
}

// Overloaded function for const wchar_t*
inline std::wstring ConvertToWString(const wchar_t* wstr) {
    return std::wstring(wstr);
}// namespace VolumeUtils