cmake_minimum_required(VERSION 3.8)
project(VoiceMirror)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/std:c++17)


# Set C++ standard and required features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define all supported build configurations
if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES Debug Release MinSizeRel RelWithDebInfo CACHE STRING 
        "Available build configurations" FORCE)
    set_property(CACHE CMAKE_CONFIGURATION_TYPES PROPERTY STRINGS 
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()


# Set output directories for all configurations
set(OUTPUT_BASE ${CMAKE_BINARY_DIR})
foreach(config Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${config} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${OUTPUT_BASE}/bin/${config})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${OUTPUT_BASE}/lib/${config})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${OUTPUT_BASE}/lib/${config})
endforeach()

# Organize source and header files explicitly
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")

# Set architecture to x86 (32-bit) if not already defined
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CMAKE_GENERATOR_PLATFORM x86 CACHE STRING "Specify platform x86 for 32-bit compilation" FORCE)
endif()


# Add executable target
add_executable(VoiceMirror ${SOURCES} ${HEADERS})

# Specify include directories
target_include_directories(VoiceMirror PRIVATE "${CMAKE_SOURCE_DIR}/include")

# Specify C++ standard and required features
target_compile_features(VoiceMirror PRIVATE cxx_std_17)

# Compiler options for each build configuration
target_compile_options(VoiceMirror PRIVATE 
    $<$<CONFIG:Debug>:/W3 /WX /RTC1 /Zi /Od>
    $<$<CONFIG:Release>:/W3 /WX /O2 /Ob2 /Oi /Ot /GL /DNDEBUG>
    $<$<CONFIG:MinSizeRel>:/W3 /WX /O1 /Ob1 /DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:/W3 /WX /O2 /Ob2 /Zi /DNDEBUG>
)

# Link Windows-specific libraries, including Propsys
target_link_libraries(VoiceMirror PRIVATE Ole32 winmm Propsys)

# Enable position-independent code if needed
# set_property(TARGET VoiceMirror PROPERTY POSITION_INDEPENDENT_CODE ON)

# Define build types if not using a multi-config generator
if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

