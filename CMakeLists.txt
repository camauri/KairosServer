# KairosRaylib/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

project(KairosRaylib 
    VERSION 1.0.0
    DESCRIPTION "High-Performance Graphics Server with Raylib"
    LANGUAGES CXX C
)

# Project-wide settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(KAIROS_BUILD_SERVER "Build Kairos Server" ON)
option(KAIROS_BUILD_TGUI "Build Kairos TGUI Client" ON)
option(KAIROS_BUILD_EXAMPLES "Build examples" ON)
option(KAIROS_BUILD_TESTS "Build tests" OFF)
option(KAIROS_BUILD_TOOLS "Build development tools" OFF)
option(KAIROS_USE_EXTERNAL_LIBS "Use external/ instead of vcpkg" ON)

# Platform detection
if(WIN32)
    set(KAIROS_PLATFORM "Windows")
elseif(UNIX AND NOT APPLE)
    set(KAIROS_PLATFORM "Linux")
elseif(APPLE)
    set(KAIROS_PLATFORM "macOS")
else()
    set(KAIROS_PLATFORM "Unknown")
endif()

message(STATUS "Building Kairos for ${KAIROS_PLATFORM}")

# Compiler-specific settings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# External dependencies setup
if(KAIROS_USE_EXTERNAL_LIBS)
    message(STATUS "Using external/ libraries")
    
    # Add external/raylib
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE) 
    add_subdirectory(external/raylib)
    
    # Add external/TGUI (if building TGUI client)
    if(KAIROS_BUILD_TGUI)
        set(TGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(external/TGUI)
    endif()
    
    # Add external/spdlog
    add_subdirectory(external/spdlog)
    
    # Header-only libraries
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE external/json/single_include)
    
else()
    # Use vcpkg/system packages
    find_package(raylib CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    
    if(KAIROS_BUILD_TGUI)
        find_package(TGUI CONFIG REQUIRED)
    endif()
endif()

# Shared library (always built)
add_subdirectory(shared)

# Main components
if(KAIROS_BUILD_SERVER)
    add_subdirectory(KairosServer)
endif()

if(KAIROS_BUILD_TGUI)
    add_subdirectory(KairosTGUI)
endif()

# Optional components
if(KAIROS_BUILD_TESTS)
    enable_testing()
    # Tests will be added by subdirectories
endif()

if(KAIROS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Install configuration
include(GNUInstallDirs)

# Export targets for find_package
install(EXPORT KairosTargets
    FILE KairosTargets.cmake
    NAMESPACE Kairos::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Kairos
)

# Generate and install config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/KairosConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/KairosConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Kairos
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KairosConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KairosConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KairosConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Kairos
)

# Summary
message(STATUS "=== Kairos Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${KAIROS_PLATFORM}")
message(STATUS "Server: ${KAIROS_BUILD_SERVER}")  
message(STATUS "TGUI Client: ${KAIROS_BUILD_TGUI}")
message(STATUS "Examples: ${KAIROS_BUILD_EXAMPLES}")
message(STATUS "Tests: ${KAIROS_BUILD_TESTS}")
message(STATUS "Tools: ${KAIROS_BUILD_TOOLS}")
message(STATUS "External libs: ${KAIROS_USE_EXTERNAL_LIBS}")
message(STATUS "===================================")