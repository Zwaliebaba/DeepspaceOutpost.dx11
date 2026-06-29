# DeepspaceOutpost - the game executable.
#
# This target now contains ONLY the legacy game code (ported C -> C++23) and the
# pieces that are specific to THIS game. The generic client engine - the render
# seam, camera/projection math, replication client, and the Win32/Direct3D 11/
# XAudio2 backend - lives in the NeuronClient library and is linked in.

# ---- Portable game logic (formerly C, now compiled as C++23) ----
set(GAME_SOURCES
    main.cpp
    LocalObjects.cpp
    elite.cpp
    space.cpp
    threed.cpp
    swat.cpp
    docked.cpp
    shipdata.cpp
    shipface.cpp
    stars.cpp
    planet.cpp
    pilot.cpp
    missions.cpp
    options.cpp
    trade.cpp
    intro.cpp
    file.cpp
    random.cpp
)

# ---- Game-specific client glue (kept here; coupled to legacy game state) ----
# Camera.cpp adapts the legacy local_object render struct + current_screen, and
# GameUniverse is this game's ECS world. WinMain.cpp is the exe entry point (a
# static library's entry point would not be pulled in by the linker), which boots
# the NeuronClient platform layer and calls game_main().
set(GAME_CLIENT_SOURCES
    WinMain.cpp
    GameApp.h
    Camera.cpp
    GameUniverse.cpp
)

add_executable(DeepspaceOutpost WIN32 pch.cpp ${GAME_SOURCES} ${GAME_CLIENT_SOURCES})

target_compile_features(DeepspaceOutpost PRIVATE cxx_std_23)

# Precompiled header (pch.h / pch.cpp). pch.h pulls in NeuronClient.h ->
# NeuronCore.h, so the engine core is available to every translation unit.
target_precompile_headers(DeepspaceOutpost PRIVATE pch.h)

# Only this target's own (game) headers live here; the engine + presentation
# contracts (gfx.h, sound.h, Renderer.h, ...) come from NeuronClient's public
# include dirs via the link below.
target_include_directories(DeepspaceOutpost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_definitions(DeepspaceOutpost PRIVATE
        UNICODE _UNICODE
    )
    # Single conformance tier: the whole target builds with /permissive.
    #
    # The faithfully ported game logic relies on C-isms (e.g. string literals to
    # char*) that /permissive- rejects, and the shared precompiled header (pch.h)
    # must be built and consumed under the same conformance flags or MSVC raises
    # C2855. It still compiles as C++23, with the same /W4 warning level.
    target_compile_options(DeepspaceOutpost PRIVATE /permissive /W4)
endif()

# Engine modules. NeuronClient pulls in NeuronCore publicly, and now also brings
# the Win32/Direct3D/XAudio2 system libraries it needs (PUBLIC), so they no
# longer need to be listed here. NeuronCore is listed explicitly per the module
# dependency contract.
target_link_libraries(DeepspaceOutpost PRIVATE
    NeuronClient
    NeuronCore
)

# ---- Copy runtime assets next to the executable ----
set(GAMEDATA_DIR "${CMAKE_SOURCE_DIR}/GameData")
if(EXISTS "${GAMEDATA_DIR}")
    add_custom_command(TARGET DeepspaceOutpost POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAMEDATA_DIR}" "$<TARGET_FILE_DIR:DeepspaceOutpost>"
        COMMENT "Copying GameData assets next to DeepspaceOutpost.exe")
endif()
