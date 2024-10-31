// COMUtilities.cpp
#include "COMUtilities.h"
#include "Logger.h"
#include <iostream>
#include <windows.h>


static thread_local bool comInitialized = false;

// void LogStackTrace()
// {
//     HANDLE process = GetCurrentProcess();
//     SymInitialize(process, NULL, TRUE);
//     void* stack[100];
//     unsigned short frames = CaptureStackBackTrace(0, 100, stack, NULL);
//     for (unsigned short i = 0; i < frames; ++i)
//     {
//         DWORD64 address = (DWORD64)(stack[i]);
//         SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
//         symbol->MaxNameLen = 255;
//         symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
//         DWORD64 displacement = 0;
//         if (SymFromAddr(process, address, &displacement, symbol))
//         {
//             Logger::Instance().Log(LogLevel::DEBUG, std::string(symbol->Name));
//         }
//         free(symbol);
//     }
//     SymCleanup(process);
// }


bool InitializeCOM()
{
    if (!comInitialized)
    {
        HRESULT hr = CoInitialize(nullptr); // Windows multimedia does NOT allow COINIT_MULTITHREADED see  https://stackoverflow.com/questions/45334838/use-coinit-apartmentthreaded-or-coinit-multithreaded-in-media-foundation
        if (hr == RPC_E_CHANGED_MODE)
        {
            Logger::Instance().Log(LogLevel::WARNING, "COM library already initialized with a different threading model.");
            // You might need to handle this differently depending on your application
            // For now, we'll set comInitialized to true and proceed
            comInitialized = true;
            //LogStackTrace();  // Call stack trace to identify where CoInitialize might have been called

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