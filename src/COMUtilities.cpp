// COMUtilities.cpp
#include "COMUtilities.h"
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
            std::cerr << "Warning: COM library already initialized with a different threading model." << std::endl;
            comInitialized = true;
            return true;
        }
        else if (FAILED(hr))
        {
            std::cerr << "Failed to initialize COM library." << std::endl;
            return false;
        }
        comInitialized = true;
    }
    return true;
}

void UninitializeCOM()
{
    if (comInitialized)
    {
        CoUninitialize();
        comInitialized = false;
    }
}
