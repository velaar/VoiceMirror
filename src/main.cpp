// Standard Library Includes
#include <iostream>      // For standard I/O operations
#include <iomanip>       // For formatted output (e.g., setw)
#include <string>        // For std::string and string manipulation
#include <string_view>   // For lightweight string handling with std::string_view
#include <thread>        // For multi-threading support (std::this_thread)
#include <atomic>        // For atomic operations (std::atomic)
#include <csignal>       // For signal handling (std::signal)
#include <memory>        // For smart pointers (e.g., std::unique_ptr)
#include <mutex>         // For locking mechanisms (std::mutex)

// Windows API Includes
#include <windows.h>                 // For Windows API functions and types
#include <mmdeviceapi.h>             // For audio device interfaces (IMMDevice)
#include <endpointvolume.h>          // For audio endpoint volume control
#include <Functiondiscoverykeys_devpkey.h> // For device property keys (PKEY_Device_FriendlyName)

// Project-Specific Includes
#include "VoicemeeterAPI.h"          // Voicemeeter API wrapper for audio control
#include "VoicemeeterManager.h"      // Manager class for initializing Voicemeeter
#include "DeviceMonitor.h"           // Device monitoring utilities
#include "VolumeMirror.h"            // Main functionality for volume mirroring
#include "cxxopts.hpp"               // Command-line options parsing library
#include "COMUtilities.h"            // Helper functions for COM initialization/uninitialization



// Global running flag for signal handling
std::atomic<bool> g_running(true);

// Signal handler for graceful shutdown
void signalHandler(int signum)
{
    std::cerr << "\nInterrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    g_running = false;
}

/**
 * @brief Lists the names and UUIDs of monitorable audio devices.
 */
void ListMonitorableDevices()
{
    if (!InitializeCOM())
    {
        std::cerr << "Failed to initialize COM library." << std::endl;
        return;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;

    // Create MMDeviceEnumerator
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create MMDeviceEnumerator." << std::endl;
        UninitializeCOM();
        return;
    }

    // Enumerate active audio rendering devices
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr))
    {
        std::cerr << "Failed to enumerate audio endpoints." << std::endl;
        pEnumerator->Release();
        UninitializeCOM();
        return;
    }

    UINT count;
    pCollection->GetCount(&count);

    std::cout << "Available audio devices for monitoring:" << std::endl;
    for (UINT i = 0; i < count; i++)
    {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(i, &pDevice);
        if (SUCCEEDED(hr))
        {
            IPropertyStore* pProps = nullptr;
            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
            if (SUCCEEDED(hr))
            {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr))
                {
                    LPWSTR deviceId;
                    pDevice->GetId(&deviceId);
                    std::wstring ws(deviceId);
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), NULL, 0, NULL, NULL);
                    std::string deviceUUID(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), &deviceUUID[0], size_needed, NULL, NULL);

                    std::wstring deviceNameW(varName.pwszVal);
                    size_needed = WideCharToMultiByte(CP_UTF8, 0, deviceNameW.c_str(), (int)deviceNameW.length(), NULL, 0, NULL, NULL);
                    std::string deviceName(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, deviceNameW.c_str(), (int)deviceNameW.length(), &deviceName[0], size_needed, NULL, NULL);

                    std::cout << "----------------------------------------" << std::endl;
                    std::cout << "Device " << i << ":" << std::endl;
                    std::cout << "Name: " << deviceName << std::endl;
                    std::cout << "UUID: " << deviceUUID << std::endl;

                    PropVariantClear(&varName);
                    CoTaskMemFree(deviceId);
                }
                pProps->Release();
            }
            pDevice->Release();
        }
    }

    pCollection->Release();
    pEnumerator->Release();
    UninitializeCOM();
}


// Function to list Voicemeeter Inputs
void ListVoicemeeterInputs(VoicemeeterAPI& vmrAPI)
{
    try
    {
        // Define a reasonable maximum number of strips to check
        int maxStrips = 8; // Adjust this number based on expected maximum strips
        std::cout << "Available Voicemeeter Virtual Inputs:" << std::endl;

        for (int i = 0; i < maxStrips; ++i)
        {
            char paramName[64];
            sprintf_s(paramName, "Strip[%d].Label", i);
            char label[512];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0)
            {
                std::cout << i << ": " << label << std::endl;
            }
            else
            {
                // Assume no more strips exist
                break;
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error listing Voicemeeter inputs: " << ex.what() << std::endl;
    }
}

// Function to list Voicemeeter Outputs
void ListVoicemeeterOutputs(VoicemeeterAPI& vmrAPI)
{
    try
    {
        // Define a reasonable maximum number of buses to check
        int maxBuses = 3; // Adjust this number based on expected maximum buses
        std::cout << "Available Voicemeeter Virtual Outputs:" << std::endl;

        for (int i = 0; i < maxBuses; ++i)
        {
            char paramName[64];
            sprintf_s(paramName, "Bus[%d].Label", i);
            char label[256];
            long result = vmrAPI.GetParameterStringA(paramName, label);
            if (result == 0 && strlen(label) > 0)
            {
                std::cout << i << ": " << label << std::endl;
            }
            else
            {
                // Assume no more buses exist
                break;
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error listing Voicemeeter outputs: " << ex.what() << std::endl;
    }
}

// Function to list all Voicemeeter channels with labels
void ListAllChannels(VoicemeeterAPI& vmr)
{
    // Determine Voicemeeter type
    long voicemeeterType = 0;
    HRESULT hr = vmr.GetVoicemeeterType(&voicemeeterType); // Changed to pass a pointer
    if (hr != 0)
    {
        std::cerr << "Failed to get Voicemeeter type." << std::endl;
        return;
    }

    std::string typeStr;
    int maxStrips = 0;
    int maxBuses = 0;

    // Set maximum strips and buses based on Voicemeeter type
    switch (voicemeeterType)
    {
    case 1:
        typeStr = "Voicemeeter";
        maxStrips = 2;
        maxBuses = 2;
        break;
    case 2:
        typeStr = "Voicemeeter Banana";
        maxStrips = 3;
        maxBuses = 5;
        break;
    case 3:
        typeStr = "Voicemeeter Potato";
        maxStrips = 5;
        maxBuses = 8;
        break;
    default:
        std::cerr << "Unknown Voicemeeter type." << std::endl;
        return;
    }

    std::cout << "Voicemeeter Type: " << typeStr << std::endl;

    // Lambda to safely get and print parameters
    auto PrintParameter = [&](const std::string& param, const std::string& type, int index) {
        char label[512] = { 0 }; // Initialize buffer to zero
        long result = vmr.GetParameterStringA(param.c_str(), label);
        if (result == 0 && strlen(label) > 0 && strlen(label) < sizeof(label))
        {
            std::cout << "| " << std::setw(7) << index << " | "
                      << std::setw(20) << label << " | "
                      << std::setw(12) << type << " |" << std::endl;
        }
        else
        {
            std::cout << "| " << std::setw(7) << index << " | "
                      << std::setw(20) << "N/A" << " | "
                      << std::setw(12) << type << " |" << std::endl;
        }
    };

    // Print header for Strips
    std::cout << "\nStrips:" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;
    std::cout << "| Index   | Label                | Type         |" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;

    for (int i = 0; i < maxStrips; ++i)
    {
        std::string paramName = "Strip[" + std::to_string(i) + "].Label";
        PrintParameter(paramName, "Input Strip", i);
    }
    std::cout << "+---------+----------------------+--------------+" << std::endl;

    // Print header for Virtual Inputs
    std::cout << "\nVirtual Inputs:" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;
    std::cout << "| Index   | Label                | Type         |" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;

    // Assuming Virtual Input is immediately after Strips
    int virtualInputIndex = maxStrips;
    std::string paramNameVI = "Strip[" + std::to_string(virtualInputIndex) + "].Label";
    PrintParameter(paramNameVI, "Virtual Input", virtualInputIndex);
    std::cout << "+---------+----------------------+--------------+" << std::endl;

    // Print header for Virtual AUX (only for Voicemeeter Banana and Potato)
    if (voicemeeterType >= 2)
    {
        int virtualAuxIndex = maxStrips + 1;
        std::string paramNameVA = "Strip[" + std::to_string(virtualAuxIndex) + "].Label";
        PrintParameter(paramNameVA, "Virtual AUX", virtualAuxIndex);
        std::cout << "+---------+----------------------+--------------+" << std::endl;
    }

    // Print header for Buses
    std::cout << "\nBuses:" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;
    std::cout << "| Index   | Label                | Type         |" << std::endl;
    std::cout << "+---------+----------------------+--------------+" << std::endl;

    for (int i = 0; i < maxBuses; ++i)
    {
        std::string paramNameBus = "Bus[" + std::to_string(i) + "].Label";
        std::string busType = "BUS " + std::to_string(i);
        PrintParameter(paramNameBus, busType, i);
    }
    std::cout << "+---------+----------------------+--------------+" << std::endl;
} 



int main(int argc, char* argv[])
{

    // Register signal handler for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try
    {
        // Define command-line options
        cxxopts::Options options("VoiceMirror", "Synchronize Windows Volume with Voicemeeter virtual channels\n\n"
            "VOICEMEETER STRIP/BUS INDEX ASSIGNMENT\n\n"
            "VOICEMEETER:\n"
            "| Strip 1 | Strip 2 | Virtual Input | BUS A | BUS B |\n"
            "+---------+---------+---------------+-------+-------+\n"
            "|    0    |    1    |       2       |   0   |   1   |\n\n"
            "VOICEMEETER BANANA:\n"
            "| Strip 1 | Strip 2 | Strip 3 | Virtual Input | Virtual AUX | BUS A1 | BUS A2 | BUS A3 | BUS B1 | BUS B2 |\n"
            "+---------+---------+---------+---------------+-------------+--------+--------+--------+--------+--------+\n"
            "|    0    |    1    |    2    |       3       |      4      |   0    |   1    |   2    |   3    |   4    |\n\n"
            "VOICEMEETER POTATO:\n"
            "| Strip 1 | Strip 2 | Strip 3 | Strip 4 | Strip 5 | Virtual Input | Virtual AUX |   VAIO3   | BUS A1 | BUS A2 | BUS A3 | BUS A4 | BUS A5 | BUS B1 | BUS B2 | BUS B3 |\n"
            "+---------+---------+---------+---------+---------+---------------+-------------+-----------+--------+--------+--------+--------+--------+--------+--------+--------+\n"
            "|    0    |    1    |    2    |    3    |    4    |       5       |      6      |     7     |   0    |   1    |   2    |   3    |   4    |   5    |   6    |   7    |\n\n");

        options.add_options()
            ("M,list-monitor", "List monitorable audio device names and UUIDs and exit")
            ("list-inputs", "List available Voicemeeter virtual inputs")
            ("list-outputs", "List available Voicemeeter virtual outputs")
            ("C,list-channels", "List all Voicemeeter channels with their labels")
            ("i,index", "Specify the Voicemeeter virtual channel index to use", cxxopts::value<int>()->default_value("3"))
            ("t,type", "Specify the type of channel to use (input/output)", cxxopts::value<std::string>()->default_value("input"))
            ("min", "Minimum dBm for Voicemeeter channel (default: -60)", cxxopts::value<float>()->default_value("-60.0"))
            ("max", "Maximum dBm for Voicemeeter channel (default: 12)", cxxopts::value<float>()->default_value("12.0"))
            ("V,voicemeeter", "Specify which Voicemeeter to use Default: Banana; 1: Voicemeeter, 2: Banana 3: Potato ", cxxopts::value<int>()->default_value("2"))
            ("d,debug", "Enable debug mode for extensive logging")
            ("v,version", "Show program's version number and exit")
            ("h,help", "Show this help and exit")
            ("s,sound", "Enable Chime on sync from VoiceMeeter to Windows")
            ("m,monitor", "Monitor a specific audio device by UUID", cxxopts::value<std::string>())
            ("T,toggle", "For use with -m when device is plugged / unplugged - mute between two channels (plugged index1 - mute, index2 - unmute) type:index1:index2 (e.g., input:0:1)", cxxopts::value<std::string>());

        // Parse command-line options
        auto result = options.parse(argc, argv);

        // Handle help
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        // Handle version
        if (result.count("version"))
        {
            std::cout << "VoiceMirror Version 0.1.9-pre-alpha" << std::endl;
            return 0;
        }

        bool enableSound = (result.count("sound") >= 1);


        // Handle list monitorable devices
        if (result.count("list-monitor"))
        {
            ListMonitorableDevices();
            return 0;
        }


        VoicemeeterManager vmrManager;

        vmrManager.SetDebugMode(result.count("debug") > 0 );

        int voicemeeterType = result["voicemeeter"].as<int>();

        if (!vmrManager.Initialize(voicemeeterType)) {
            std::cerr << "Failed to initialize Voicemeeter Manager." << std::endl;
            return -1;
        }

        if (result.count("list-inputs"))
        {
            ListVoicemeeterInputs(vmrManager.GetAPI());
            vmrManager.Shutdown(); // Clean up properly
            return 0;
        }

        // Handle list outputs
        if (result.count("list-outputs"))
        {
            ListVoicemeeterOutputs(vmrManager.GetAPI());
            vmrManager.Shutdown(); // Clean up properly
            return 0;
        }

        // Handle list channels
        if (result.count("list-channels"))
        {
            ListAllChannels(vmrManager.GetAPI());
            vmrManager.Shutdown(); // Clean up properly
            return 0;
        }
        // Get command-line options
        int channelIndex = result["index"].as<int>();
        std::string typeStr = result["type"].as<std::string>();
        float minDbm = result["min"].as<float>();
        float maxDbm = result["max"].as<float>();

        // Validate 'type' option
        ChannelType channelType;
        if (typeStr == "input" || typeStr == "Input")
        {
            channelType = ChannelType::Input;
        }
        else if (typeStr == "output" || typeStr == "Output")
        {
            channelType = ChannelType::Output;
        }
        else
        {
            std::cerr << "Invalid type specified. Use 'input' or 'output'." << std::endl;
            vmrManager.GetAPI().Shutdown();
            return -1;
        }

        // Handle monitor and toggle parameters
        std::string monitorDeviceUUID;
        bool isMonitoring = false;
        if (result.count("monitor"))
        {
            monitorDeviceUUID = result["monitor"].as<std::string>();
            isMonitoring = true;
        }

        ToggleConfig toggleConfig;
        bool isToggleEnabled = false;
        if (result.count("toggle"))
        {
            if (!isMonitoring)
            {
                std::cerr << "--toggle parameter requires --monitor to be specified." << std::endl;
                vmrManager.GetAPI().Shutdown();
                return -1;
            }

            std::string toggleParam = result["toggle"].as<std::string>();
            // Expected format: type:index1:index2 (e.g., input:0:1)
            size_t firstColon = toggleParam.find(':');
            size_t secondColon = toggleParam.find(':', firstColon + 1);

            if (firstColon == std::string::npos || secondColon == std::string::npos)
            {
                std::cerr << "Invalid --toggle format. Expected format: type:index1:index2 (e.g., input:0:1)" << std::endl;
                vmrManager.GetAPI().Shutdown();
                return -1;
            }

            toggleConfig.type = toggleParam.substr(0, firstColon);
            std::string index1Str = toggleParam.substr(firstColon + 1, secondColon - firstColon - 1);
            std::string index2Str = toggleParam.substr(secondColon + 1);

            try
            {
                toggleConfig.index1 = std::stoi(index1Str);
                toggleConfig.index2 = std::stoi(index2Str);
                isToggleEnabled = true;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Invalid indices in --toggle parameter: " << ex.what() << std::endl;
                vmrManager.Shutdown();
                return -1;
            }

            // Validate toggle type
            if (toggleConfig.type != "input" && toggleConfig.type != "output")
            {
                std::cerr << "Invalid toggle type: " << toggleConfig.type << ". Use 'input' or 'output'." << std::endl;
                vmrManager.Shutdown();
                return -1;
            }
        }

        // Initialize VolumeMirror+


        VolumeMirror mirror(channelIndex, channelType, minDbm, maxDbm, vmrManager, voicemeeterType, enableSound);
        mirror.Start();

        // Initialize DeviceMonitor if monitoring is enabled
        DeviceMonitor* deviceMonitor = nullptr;
        if (isMonitoring)
        {
            deviceMonitor = new DeviceMonitor(monitorDeviceUUID, toggleConfig, vmrManager, mirror);

            try
            {
                std::cout << "Started monitoring device UUID: " << monitorDeviceUUID << std::endl;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Failed to initialize DeviceMonitor: " << ex.what() << std::endl;
                return -1;
            }
        }

        std::cout << "VoiceMirror is running. Press Ctrl+C to exit." << std::endl;

        // Keep the application running until interrupted
        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Cleanup
        if (deviceMonitor)
        {
            deviceMonitor->Release(); // Properly release COM object
        }

        mirror.Stop();
        vmrManager.GetAPI().SetParameterFloat("Command.Shutdown", 1);
        vmrManager.Shutdown();

        std::cout << "VoiceMirror has shut down gracefully." << std::endl;
    }

    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return -1;
    }

    return 0;
}
