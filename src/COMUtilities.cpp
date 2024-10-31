// COMUtilities.cpp
#include "COMUtilities.h"
#include "Logger.h"
#include <iostream>
#include <windows.h>

static thread_local bool comInitialized = false;

bool InitializeCOM()
{
    if (!comInitialized)
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE)
        {
            Logger::Instance().Log(LogLevel::WARNING, "COM library already initialized with a different threading model.");
            // You might need to handle this differently depending on your application
            // For now, we'll set comInitialized to true and proceed
            comInitialized = true;
            return true;
        }
        else if (FAILED(hr))
        {
            Logger::Instance().Log(LogLevel::ERR, "Failed to initialize COM library. HRESULT: " + std::to_string(hr));
            return false;
        }
        comInitialized = true;
        Logger::Instance().Log(LogLevel::DEBUG, "COM library initialized successfully.");
    }
    return true;
}

void UninitializeCOM()
{
    if (comInitialized)
    {
        CoUninitialize();
        comInitialized = false;
        Logger::Instance().Log(LogLevel::DEBUG, "COM library uninitialized.");
    }
}