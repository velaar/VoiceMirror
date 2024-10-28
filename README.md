# VoiceMirror

VoiceMirror is an application designed to synchronize Windows audio volume with Voicemeeter channels. The code is responsive (sometimes too much) and consumes about 2MB of RAM at runtime. It has no outside dependencies and runs on pure WinAPI/VoiceMeeter API. It provides functionalities for monitoring audio devices, mirroring volume levels, and managing Voicemeeter channels. This application is provided without any warranty.

**Note:** Make sure the `VoicemeeterRemote64.dll` or `VoicemeeterRemote.dll` file is in the same folder as the executable.

## Features

- Synchronize Windows audio volume with Voicemeeter virtual channels.
- List available Voicemeeter inputs and outputs.
- Monitor audio devices by UUID and toggle volume based on device connection status.
- Configure Voicemeeter channel types and volume limits.
- Debugging support with extensive logging.

## Table of Contents

- [Installation](#installation)
- [Usage](#usage)
- [Command-Line Options](#command-line-options)
- [Main Classes](#main-classes)
- [Signal Handling](#signal-handling)
- [License](#license)

## Installation

1. Download a release.
2. Make sure `VoicemeeterRemote64.dll` or `VoicemeeterRemote.dll`  is in the same directory as the executable.


## Usage

Run the application with appropriate command-line arguments. For example, to list monitorable devices, use:
VoiceMirror --list-monitor

To synchronize volume with Voicemeeter Banana and monitor a device:
VoiceMirror -V 2 --monitor <device-UUID>

Press `Ctrl+C` to gracefully exit the program.

## Command-Line Options

| Option                          | Description                                                                               |
|---------------------------------|-------------------------------------------------------------------------------------------|
| `-M, --list-monitor`            | List monitorable audio device names and UUIDs and exit.                                    |
| `--list-inputs`                 | List available Voicemeeter virtual inputs.                                                 |
| `--list-outputs`                | List available Voicemeeter virtual outputs.                                                |
| `-C, --list-channels`           | List all Voicemeeter channels with their labels.                                           |
| `-i, --index <index>`           | Specify the Voicemeeter virtual channel index to use (default: 3).                         |
| `-t, --type <input/output>`     | Specify the type of channel to use (default: input).                                       |
| `--min <value>`                 | Minimum dBm for Voicemeeter channel (default: -60).                                        |
| `--max <value>`                 | Maximum dBm for Voicemeeter channel (default: 12).                                         |
| `-V, --voicemeeter <value>`     | Specify which Voicemeeter to use: 1 (Voicemeeter), 2 (Banana), or 3 (Potato).              |
| `-d, --debug`                   | Enable debug mode for extensive logging.                                                   |
| `-v, --version`                 | Show program's version number and exit.                                                    |
| `-h, --help`                    | Show help and exit.                                                                        |
| `-s, --sound`                   | Enable chime on sync from Voicemeeter to Windows.                                          |
| `-m, --monitor <device-UUID>`   | Monitor a specific audio device by UUID.                                                   |
| `-T, --toggle <type:index1:index2>` | Toggle mute between two channels when device is plugged/unplugged. Required with `-m`.  |

## Main Classes

- **`VoicemeeterManager`**: Manages the initialization and shutdown of the Voicemeeter API.
- **`VoicemeeterAPI`**: Wraps the Voicemeeter API for audio control.
- **`VolumeMirror`**: Handles volume mirroring between Windows and Voicemeeter channels.
- **`DeviceMonitor`**: Monitors the state of audio devices, toggling volume settings as needed.
- **`COMUtilities`**: Provides functions for initializing and uninitializing the COM library.

## Signal Handling

- The application uses signal handling to manage graceful shutdowns upon receiving `SIGINT` or `SIGTERM`.
- When a shutdown signal is detected, all ongoing processes are stopped, and resources are released properly.

## License

"THE BEER-WARE LICENSE" (Revision 43_VR):
Velaar wrote this file. As long as you retain this notice, you can do whatever you want with this stuff. If we meet some day, and you think this stuff is worth it, you can buy me a beer in return. Remote beers are accepted.
