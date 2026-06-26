# DeepspaceOutpost - DirectX 11 / XAudio2.
#
# Target: a Win32 GUI executable. Portable game logic (ported from C to C++23)
# plus the new platform layer (Win32 / Direct3D 11 / XAudio2).

# ---- Portable game logic (formerly C, now compiled as C++23) ----
set(GAME_SOURCES
    main.cpp
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
    vector.cpp
    intro.cpp
    file.cpp
    random.cpp
)

# ---- Platform layer (Win32 / Direct3D 11 / XAudio2) - idiomatic modern C++23 ----
set(PLATFORM_SOURCES
    platform/platform_win.cpp
    platform/Renderer.cpp
    platform/gfx_dx11.cpp
    platform/Image.cpp
    platform/Font.cpp
    platform/audio_xa2.cpp
    platform/input_win.cpp
    platform/dialog_win.cpp
)

add_executable(DeepspaceOutpost WIN32 ${GAME_SOURCES} ${PLATFORM_SOURCES})

target_compile_features(DeepspaceOutpost PRIVATE cxx_std_23)

# Contract headers (gfx.h, sound.h, ...) live in this dir; platform headers in platform/.
target_include_directories(DeepspaceOutpost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

if(MSVC)
    target_compile_definitions(DeepspaceOutpost PRIVATE
        _CRT_SECURE_NO_WARNINGS   # legacy strcpy/sprintf/fopen in the ported C
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Two conformance tiers:
    #  - The new platform layer is held to strict, idiomatic modern C++ (/permissive-,
    #    /W4) - this is freshly written code and should be clean.
    #  - The faithfully ported game logic stays /permissive (it relies on a
    #    few C-isms, e.g. string literals to char*); it is deliberately left as a
    #    thin port to avoid behavioural regressions in untested reverse-engineered
    #    math. It still compiles as C++23.
    set_source_files_properties(${GAME_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/permissive")
    set_source_files_properties(${PLATFORM_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/permissive-;/W4")
endif()

# Libraries the platform layer will need across the milestones.
target_link_libraries(DeepspaceOutpost PRIVATE
    # Engine modules for the MMO migration. NeuronClient pulls in NeuronCore
    # publicly; NeuronCore is listed explicitly per the module dependency contract.
    NeuronClient
    NeuronCore

    d3d11
    dxgi
    d3dcompiler
    dxguid
    xaudio2
    winmm        # timing + MIDI (M6)
    comdlg32     # file dialog (M4)
    ole32        # COM / WIC
    windowscodecs # WIC image loading (M3)
)

# ---- Copy runtime assets next to the executable ----
set(GAMEDATA_DIR "${CMAKE_SOURCE_DIR}/GameData")
if(EXISTS "${GAMEDATA_DIR}")
    add_custom_command(TARGET DeepspaceOutpost POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAMEDATA_DIR}" "$<TARGET_FILE_DIR:DeepspaceOutpost>"
        COMMENT "Copying GameData assets next to DeepspaceOutpost.exe")
endif()
