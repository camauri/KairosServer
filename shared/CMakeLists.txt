# shared/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

project(KairosShared
    VERSION 1.0.0
    DESCRIPTION "Shared components for Kairos graphics system"
    LANGUAGES CXX
)

# Source files
set(SHARED_SOURCES
    src/Protocol.cpp
    src/Serialization.cpp
)

# Header files (for IDE support)
set(SHARED_HEADERS
    include/Protocol.hpp
    include/Types.hpp
    include/Constants.hpp
)

# Create shared library
add_library(KairosShared STATIC ${SHARED_SOURCES} ${SHARED_HEADERS})

# Set target properties
set_target_properties(KairosShared PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Include directories - SEMPLIFICATO!
target_include_directories(KairosShared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler-specific settings
if(MSVC)
    target_compile_options(KairosShared PRIVATE /W4)
    target_compile_definitions(KairosShared PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
else()
    target_compile_options(KairosShared PRIVATE 
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

# Platform-specific definitions
if(WIN32)
    target_compile_definitions(KairosShared PRIVATE KAIROS_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(KairosShared PRIVATE KAIROS_PLATFORM_LINUX)
elseif(APPLE)
    target_compile_definitions(KairosShared PRIVATE KAIROS_PLATFORM_MACOS)
endif()

# Dependencies
target_link_libraries(KairosShared
    PUBLIC
        nlohmann_json
)

# Platform-specific libraries for Protocol.cpp
if(WIN32)
    target_link_libraries(KairosShared PRIVATE ws2_32)
endif()

# Install targets
install(TARGETS KairosShared
    EXPORT KairosTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Create alias for internal use
add_library(Kairos::Shared ALIAS KairosShared)