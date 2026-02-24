cmake_minimum_required(VERSION 3.21)
project(wormhole-gui LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC doesn't report __cplusplus correctly without this flag; Qt6 requires it
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# --- Standalone IPC client library (C, no MsQuic dependency) ---
add_library(ipc_client STATIC ipc_client.c ipc_client.h)
target_include_directories(ipc_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(ipc_client PRIVATE ws2_32)
endif()

# --- GUI executable ---
set(GUI_SOURCES
    main.cpp
    MainWindow.h      MainWindow.cpp
    DaemonClient.h    DaemonClient.cpp
    IpcWorker.h       IpcWorker.cpp
    TransferWidget.h  TransferWidget.cpp
    FileListWidget.h  FileListWidget.cpp
    PeerListWidget.h  PeerListWidget.cpp
    SettingsDialog.h  SettingsDialog.cpp
    TrayManager.h     TrayManager.cpp
    utils.h
)

set(GUI_RESOURCES
    resources/wormhole.qrc
)

add_executable(wormhole-gui ${GUI_SOURCES} ${GUI_RESOURCES})

target_link_libraries(wormhole-gui PRIVATE
    Qt6::Widgets
    ipc_client
)

# No console window on Windows
set_target_properties(wormhole-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# --- Headless IPC test (Qt6::Test, no Widgets) ---
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    add_executable(test_gui_ipc
        test_gui_ipc.cpp
        IpcWorker.cpp
        DaemonClient.cpp
    )
    target_link_libraries(test_gui_ipc PRIVATE
        Qt6::Core Qt6::Test ipc_client
    )
    target_include_directories(test_gui_ipc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Deploy Qt runtime DLLs on Windows (windeployqt)
if(WIN32)
    add_custom_command(TARGET wormhole-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running windeployqt..."
        COMMAND "$<TARGET_FILE_DIR:Qt6::Widgets>/windeployqt.exe"
                --no-translations --no-opengl-sw
                "$<TARGET_FILE:wormhole-gui>"
        COMMENT "Deploying Qt runtime DLLs"
    )

    # Deploy Qt runtime DLLs for test binary (Windows)
    if(Qt6Test_FOUND)
        add_custom_command(TARGET test_gui_ipc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running windeployqt for test binary..."
            COMMAND "$<TARGET_FILE_DIR:Qt6::Core>/windeployqt.exe"
                    --no-translations --no-opengl-sw
                    "$<TARGET_FILE:test_gui_ipc>"
            COMMENT "Deploying Qt runtime DLLs for test_gui_ipc"
        )
    endif()
endif()
