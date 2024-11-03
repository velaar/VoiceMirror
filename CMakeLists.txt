cmake_minimum_required(VERSION 3.19)
project(VoiceMirror)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Organize output directories based on build type
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Organize source and header files explicitly
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")

# Add executable target
add_executable(VoiceMirror ${SOURCES} ${HEADERS})

# Specify include directories
target_include_directories(VoiceMirror PRIVATE "${CMAKE_SOURCE_DIR}/include")

# Specify C++ standard and required features
target_compile_features(VoiceMirror PRIVATE cxx_std_17)

# **Set Runtime Library Property for MSVC Only if Not Already Set**
if (MSVC)
    # CMake 3.15 and above support CMAKE_MSVC_RUNTIME_LIBRARY
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
        # Check if CMAKE_MSVC_RUNTIME_LIBRARY is already set via presets
        if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
            set_property(TARGET VoiceMirror PROPERTY CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    else()
        # Fallback for older CMake versions
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(VoiceMirror PRIVATE /MDd)
        else()
            target_compile_options(VoiceMirror PRIVATE /MD)
        endif()
    endif()
endif()

# Compiler options for each build configuration
target_compile_options(VoiceMirror PRIVATE 
    $<$<CONFIG:Debug>:/Zi /EHsc /Oy- /Ob0>
    $<$<CONFIG:Release>:/W3 /WX /O2 /Ob2 /Oi /Ot /GL /DNDEBUG /fp:fast /arch:AVX2>
)

# Linker options for Release
target_link_options(VoiceMirror PRIVATE
    $<$<CONFIG:Release>:/LTCG>
)

# Link Windows-specific libraries, including Propsys
target_link_libraries(VoiceMirror PRIVATE Ole32 winmm Propsys)

# Optionally, enable position-independent code if needed
# set_property(TARGET VoiceMirror PROPERTY POSITION_INDEPENDENT_CODE ON)
