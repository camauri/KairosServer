# KairosServer/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

project(KairosServer
    VERSION 1.0.0
    DESCRIPTION "High-Performance Graphics Server with Raylib"
    LANGUAGES CXX C
)

# Source files
set(SERVER_SOURCES
    src/main.cpp
    src/Core/Server.cpp
    src/Core/RaylibRenderer.cpp
    src/Core/CommandProcessor.cpp
    src/Core/NetworkManager.cpp
    src/Core/LayerManager.cpp
    src/Core/FontManager.cpp
    src/Graphics/TextRenderer.cpp
    src/Graphics/PrimitiveRenderer.cpp
    src/Graphics/BatchRenderer.cpp
    src/Network/Client.cpp
    src/Network/TCPSocket.cpp
    src/Network/UnixSocket.cpp
    src/Network/SocketManager.cpp
    src/Utils/Logger.cpp
    src/Utils/Config.cpp
    src/Utils/Timer.cpp
    src/Utils/Platform.cpp
)  

# Header files (for IDE support)
set(SERVER_HEADERS
    include/Core/Server.hpp
    include/Core/RaylibRenderer.hpp
    include/Core/CommandProcessor.hpp
    include/Core/NetworkManager.hpp
    include/Core/LayerManager.hpp
    include/Core/FontManager.hpp
    include/Graphics/RenderCommand.hpp
    include/Graphics/TextRenderer.hpp
    include/Graphics/PrimitiveRenderer.hpp
    include/Graphics/BatchRenderer.hpp
    include/Network/Client.hpp
    include/Network/TCPSocket.hpp
    include/Network/UnixSocket.hpp
    include/Network/SocketManager.hpp
    include/Utils/Logger.hpp
    include/Utils/Config.hpp
    include/Utils/Timer.hpp
    include/Utils/Platform.hpp
)

# Create executable
add_executable(KairosServer ${SERVER_SOURCES} ${SERVER_HEADERS})

# Set target properties
set_target_properties(KairosServer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "kairos-server"
)

# Include directories - SEMPLIFICATO!
target_include_directories(KairosServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler-specific settings
if(MSVC)
    target_compile_options(KairosServer PRIVATE /W4)
    target_compile_definitions(KairosServer PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(KairosServer PRIVATE 
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

# Platform-specific definitions
if(WIN32)
    target_compile_definitions(KairosServer PRIVATE 
        PLATFORM_DESKTOP
        KAIROS_PLATFORM_WINDOWS
    )
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(KairosServer PRIVATE 
        PLATFORM_DESKTOP
        KAIROS_PLATFORM_LINUX
    )
    # Check for embedded Linux
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        target_compile_definitions(KairosServer PRIVATE KAIROS_EMBEDDED)
    endif()
elseif(APPLE)
    target_compile_definitions(KairosServer PRIVATE 
        PLATFORM_DESKTOP
        KAIROS_PLATFORM_MACOS
    )
endif()

# Dependencies
target_link_libraries(KairosServer
    PRIVATE
        Kairos::Shared  # Link alla shared library
        raylib
        spdlog::spdlog
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(KairosServer PRIVATE 
        ws2_32 
        winmm
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(KairosServer PRIVATE 
        pthread
        dl
        m
        X11
        GL
    )
    # Additional libraries for embedded
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        target_link_libraries(KairosServer PRIVATE 
            EGL
            GLESv2
        )
    endif()
elseif(APPLE)
    target_link_libraries(KairosServer PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreVideo"
    )
endif()

# Copy assets to build directory (se esistono)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    add_custom_command(TARGET KairosServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:KairosServer>/assets
        COMMENT "Copying assets to build directory"
    )
endif()

# Install executable
install(TARGETS KairosServer
    EXPORT KairosTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install assets (se esistono)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    install(DIRECTORY assets/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kairos
        FILES_MATCHING 
            PATTERN "*.ttf"
            PATTERN "*.otf"
            PATTERN "*.json"
            PATTERN "*.vs"
            PATTERN "*.fs"
    )
endif()

# Install public headers (se necessario)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/KairosServer
    FILES_MATCHING PATTERN "*.hpp"
)

# Examples (optional)
if(KAIROS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests (optional)
if(KAIROS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Create alias
add_library(Kairos::Server ALIAS KairosServer)