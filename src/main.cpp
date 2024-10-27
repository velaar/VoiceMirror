// src/main.cpp

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include "VoicemeeterAPI.h"
#include "VolumeMirror.h"
#include "cxxopts.hpp"

// Function to list Voicemeeter Inputs
void ListVoicemeeterInputs(VoicemeeterAPI& vmr)
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
            char label[256];
            long result = vmr.GetParameterStringA(paramName, label);
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
void ListVoicemeeterOutputs(VoicemeeterAPI& vmr)
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
            long result = vmr.GetParameterStringA(paramName, label);
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

int main(int argc, char* argv[])
{
    try
    {
        // Define command-line options
        cxxopts::Options options("VolumeMirrorCPP", "Synchronize Windows Volume with Voicemeeter virtual channels");

        options.add_options()
            ("list-inputs", "List available Voicemeeter virtual inputs")
            ("list-outputs", "List available Voicemeeter virtual outputs")
            ("i,index", "Specify the Voicemeeter virtual channel index to use", cxxopts::value<int>()->default_value("0"))
            ("type", "Specify the type of channel to use (input/output)", cxxopts::value<std::string>()->default_value("output"))
            ("min", "Minimum dBm for Voicemeeter channel (default: -60)", cxxopts::value<float>()->default_value("-60.0"))
            ("max", "Maximum dBm for Voicemeeter channel (default: 12)", cxxopts::value<float>()->default_value("12.0"))
            ("d,debug", "Enable debug mode for extensive logging")
            ("v,version", "Show program's version number and exit")
            ("h,help", "Show help and usage");

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
            std::cout << "VolumeMirrorCPP Version 1.0" << std::endl;
            return 0;
        }

        // Initialize Voicemeeter API
        VoicemeeterAPI vmr;
        if (!vmr.Initialize())
        {
            std::cerr << "Failed to initialize Voicemeeter API." << std::endl;
            return -1;
        }

        // Login to Voicemeeter
        long loginResult = vmr.Login();
        if (loginResult == 1)
        {
            std::cout << "Voicemeeter is not running. Attempting to launch..." << std::endl;
            vmr.RunVoicemeeter(1); // 1 = Voicemeeter
            std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait for Voicemeeter to launch
            loginResult = vmr.Login();
        }

        if (loginResult != 0)
        {
            std::cerr << "Failed to connect to Voicemeeter. Error Code: " << loginResult << std::endl;
            vmr.Shutdown();
            return -1;
        }

        // Handle list inputs
        if (result.count("list-inputs"))
        {
            ListVoicemeeterInputs(vmr);
            vmr.Logout();
            vmr.Shutdown();
            return 0;
        }

        // Handle list outputs
        if (result.count("list-outputs"))
        {
            ListVoicemeeterOutputs(vmr);
            vmr.Logout();
            vmr.Shutdown();
            return 0;
        }

        // Get command-line options
        int channelIndex = result["index"].as<int>();
        std::string typeStr = result["type"].as<std::string>();
        float minDbm = result["min"].as<float>();
        float maxDbm = result["max"].as<float>();
        bool debugMode = result.count("debug") > 0;

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
            vmr.Logout();
            vmr.Shutdown();
            return -1;
        }

        // Initialize VolumeMirror
        VolumeMirror mirror(channelIndex, channelType, minDbm, maxDbm, debugMode);
        mirror.Start();

        std::cout << "VolumeMirrorCPP is running. Press Ctrl+C to exit." << std::endl;

        // Keep the application running
        std::atomic<bool> running(true);
        // Handle Ctrl+C to gracefully shutdown
        // For simplicity, not implementing signal handling here
        while (running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Cleanup
        vmr.Logout();
        vmr.Shutdown();
    }
    catch (const cxxopts::OptionException& ex)
    {
        std::cerr << "Error parsing options: " << ex.what() << std::endl;
        return -1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return -1;
    }

    return 0;
}
