cmake_minimum_required(VERSION 3.20)
project(USB-Lan-ESP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project is designed for Windows only")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/Release")

#############################################
# Monitor Process (Driver Lifecycle Manager)
#############################################
add_executable(USB-Lan-Monitor WIN32
    src/Monitor/Monitor.cpp
)

target_compile_definitions(USB-Lan-Monitor PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(USB-Lan-Monitor PRIVATE
    ntdll advapi32 fltlib
)

if(MSVC)
    target_compile_options(USB-Lan-Monitor PRIVATE /W3 /O2)
    target_link_options(USB-Lan-Monitor PRIVATE /SUBSYSTEM:WINDOWS)
endif()

#############################################
# Main ESP Process
#############################################

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(USB_LAN_ESP_INCLUDES
    src
    src/Libs/json
    src/OS-ImGui/imgui
    src/OS-ImGui/stb
    src/NeacController
)

set(USB_LAN_ESP_SOURCES
    src/main.cpp

    src/Config/ConfigMenu.cpp
    src/Config/ConfigSaver.cpp

    src/Core/Cheats.cpp
    src/Core/Dumper.cpp
    src/Core/driver_controller.cpp
    src/Core/GlobalVars.cpp
    src/Core/MemoryMgr.cpp
    src/Core/VisCheckManager.cpp
    src/Core/NeacSafe/Driver.cpp

    src/Features/Aimbot.cpp
    src/Features/GrenadeHelper.cpp
    src/Features/Misc.cpp
    src/Features/Radar.cpp
    src/Features/RCS.cpp
    src/Features/TriggerBot.cpp

    src/Game/Bone.cpp
    src/Game/Entity.cpp
    src/Game/Game.cpp

    src/Helpers/Mouse.cpp
    src/Helpers/StorageMgr.cpp
    src/Helpers/UIAccess.cpp

    src/Offsets/Offsets.cpp

    src/Vischeck/BVHCache.cpp
    src/Vischeck/OptimizedGeometry.cpp
    src/Vischeck/Parser.cpp
    src/Vischeck/VisCheck.cpp

    src/OS-ImGui/imgui/imgui.cpp
    src/OS-ImGui/imgui/imgui_demo.cpp
    src/OS-ImGui/imgui/imgui_draw.cpp
    src/OS-ImGui/imgui/imgui_impl_dx11.cpp
    src/OS-ImGui/imgui/imgui_impl_win32.cpp
    src/OS-ImGui/imgui/imgui_tables.cpp
    src/OS-ImGui/imgui/imgui_widgets.cpp

    src/OS-ImGui/OS-ImGui.cpp
    src/OS-ImGui/OS-ImGui_Base.cpp
    src/OS-ImGui/OS-ImGui_External.cpp

    # Embedded driver resource
    src/Core/NeacSafe/NeacSafe.rc
)

add_executable(${PROJECT_NAME} WIN32 ${USB_LAN_ESP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${USB_LAN_ESP_INCLUDES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    fltlib
    winhttp
    Shlwapi
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /O2
        /GS-
        /fp:fast
        /Oi
        /Ot
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /SUBSYSTEM:WINDOWS
    )
endif()
