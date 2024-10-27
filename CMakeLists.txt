cmake_minimum_required(VERSION 3.10)
project(VolumeMirrorCPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include directories
include_directories(include)

# Source files
file(GLOB SOURCES "src/*.cpp")

# Add executable
add_executable(VolumeMirrorCPP ${SOURCES})

# Link libraries
# None needed as we are dynamically loading VoicemeeterRemote.dll and using COM interfaces
# If you use other libraries, link them here

# For NAudio-like functionality in C++, you need to link against required Windows libraries
target_link_libraries(VolumeMirrorCPP Ole32)

# Optionally, define preprocessor macros
# target_compile_definitions(VolumeMirrorCPP PRIVATE ...)
