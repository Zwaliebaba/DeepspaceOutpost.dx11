# ServerManager - the standalone management/status GUI app (sm.md).
#
# A Win32 GUI executable that connects to a running dedicated server over its
# separate management port (NeuronClient::AdminClient) and renders a live status
# dashboard with the same client engine as the game (ClientEngine + Canvas/GuiWindow),
# minus the game itself. Links NeuronClient only.

set(SERVERMANAGER_SOURCES
    pch.cpp
    WinMain.cpp
    ManagerApp.h
    ManagerApp.cpp
    StatusWindows.h
    StatusWindows.cpp
)

add_executable(ServerManager WIN32 ${SERVERMANAGER_SOURCES})

target_compile_features(ServerManager PRIVATE cxx_std_23)

# Precompiled header (pch.h / pch.cpp) pulls in NeuronClient.h -> NeuronCore.h.
target_precompile_headers(ServerManager PRIVATE pch.h)

target_include_directories(ServerManager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_definitions(ServerManager PRIVATE UNICODE _UNICODE)
    # Newly written app code builds under the strict conformance tier + /W4, like the
    # engine layers (it shares no legacy-C game sources).
    target_compile_options(ServerManager PRIVATE /permissive- /W4)
endif()

# NeuronClient brings NeuronCore and the Win32/Direct3D/XAudio2 system libs publicly.
target_link_libraries(ServerManager PRIVATE
    NeuronClient
    NeuronCore
)

# Copy runtime assets (fonts, strings, textures) next to the executable so the GUI
# stack finds them, exactly like the game client's post-build step.
set(GAMEDATA_DIR "${CMAKE_SOURCE_DIR}/GameData")
if(EXISTS "${GAMEDATA_DIR}")
    add_custom_command(TARGET ServerManager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAMEDATA_DIR}" "$<TARGET_FILE_DIR:ServerManager>"
        COMMENT "Copying GameData assets next to ServerManager.exe")
endif()
