# NeuronClient - client-side engine layer for the Neuron MMO migration.
#
# Built on NeuronCore and consumed by the DeepspaceOutpost game executable. This
# is the generic (non-game-specific) client engine: the render seam, the camera /
# projection math, the replicated-scene applier, the network client, and the
# Win32 / Direct3D 11 / XAudio2 platform backend. The DeepspaceOutpost target
# keeps only the legacy game code and the exe entry point (WinMain).
#
# NOTE: because the Direct3D/Win32 backend lives here, NeuronClient now only
# *compiles* on Windows (it pulls in <d3d11.h>, <winrt/base.h>, XAudio2, ...).

set(NEURONCLIENT_SOURCES
    pch.cpp

    # Render seam + generic client engine (portable C++; no Win32/D3D).
    RenderQueue.cpp
    RenderContext.cpp
    ReplicationClient.cpp
    vector.cpp

    # Native Direct3D 11 graphics core + immediate-mode renderer (Neuron::Graphics).
    # Imported from the donor NeuronClient; the foundation the GUI/text/menu import
    # is built on. See docs/phase1-graphicscore.md.
    graphics/GraphicsCore.cpp
    graphics/ImmediateRenderer.cpp
    graphics/TextureManager.cpp

    # Imported text + GUI menu system (Phases 3-4). Drawn through ImmediateRenderer;
    # menu navigation reads keyboard.h directly and quit goes through the gui/GameApp.h
    # shim. Textures load directly through Neuron::Graphics::TextureManager.
    # See docs/phase2-4-gui-text.md.
    gui/TextRenderer.cpp
    gui/Strings.cpp
    gui/GuiButton.cpp
    gui/GuiWindow.cpp
    gui/Canvas.cpp
    gui/GuiOverlay.cpp

    # Win32 / Direct3D 11 / XAudio2 platform backend.
    platform/platform_win.cpp
    platform/Renderer.cpp
    platform/gfx_dx11.cpp
    platform/GfxRenderSink.cpp
    platform/Image.cpp
    platform/Font.cpp
    platform/audio_xa2.cpp
    platform/input_win.cpp
    platform/dialog_win.cpp
)

set(NEURONCLIENT_HEADERS
    NeuronClient.h
    pch.h

    # Native Direct3D 11 graphics core headers.
    graphics/GraphicsCore.h
    graphics/ImmediateRenderer.h
    graphics/ConstantBuffers.h
    graphics/TextureManager.h

    # Imported text + GUI menu system headers (+ native shims).
    gui/TextRenderer.h
    gui/Strings.h
    gui/Widget.h
    gui/GuiButton.h
    gui/GuiWindow.h
    gui/Canvas.h
    gui/GuiOverlay.h
    gui/GameApp.h

    # Generic engine headers.
    ViewMetrics.h
    CameraFollow.h
    ReplicatedScene.h
    RenderQueue.h
    RenderContext.h
    ReplicationClient.h
    GfxRenderSink.h
    vector.h

    # Presentation contracts the game draws/plays/reads through.
    gfx.h
    sound.h
    keyboard.h
    compat.h

    # Platform backend headers.
    platform/Renderer.h
    platform/gfx_dx11.h
    platform/Image.h
    platform/Font.h
    platform/audio_win.h
    platform/input_win.h
    platform/platform_win.h
)

add_library(NeuronClient STATIC ${NEURONCLIENT_SOURCES} ${NEURONCLIENT_HEADERS})

target_compile_features(NeuronClient PUBLIC cxx_std_23)

# Single conformance tier matching the platform code's original home: the legacy
# presentation contracts (gfx.h, ...) and the shared PCH build under /permissive,
# same as the game target did, so the same precompiled header is reusable.
if(MSVC)
    target_compile_options(NeuronClient PRIVATE /permissive /W4)
    target_compile_definitions(NeuronClient PUBLIC UNICODE _UNICODE)
endif()

# PUBLIC so the DeepspaceOutpost target (and tests) can include these headers.
# The platform subdir is public too so the game's WinMain/glue can reach it.
target_include_directories(NeuronClient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
)

# --- Native Direct3D 11 shaders -----------------------------------------------
# Each shaders/*.hlsl is compiled by fxc into a C++ byte-array header in
# shaders/CompiledShaders/<name>.h (array g_<name>, with '-' replaced by '_').
# ImmediateRenderer.cpp #includes those headers, so that directory is on its private
# include path. The compiled headers are committed to the repo, so a configure
# without fxc still builds against them; fxc is only needed to *regenerate* them
# after editing the HLSL. Stage is taken from the file-name suffix: *VS.hlsl ->
# vs_5_0/VSMain, *PS.hlsl -> ps_5_0/PSMain. Partials (shaders/partials/*.hlsli) are
# #include-only.
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CompiledShaders")

# PRIVATE: only NeuronClient's own sources reach the compiled shaders.
target_include_directories(NeuronClient PRIVATE ${SHADER_OUT_DIR})

find_program(FXC_EXECUTABLE
    NAMES fxc
    HINTS
        "$ENV{WindowsSdkVerBinPath}x64"
        "$ENV{WindowsSdkVerBinPath}x86"
        "$ENV{WindowsSdkDir}bin/x64"
)

file(GLOB SHADER_SOURCES  CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/*.hlsl")
file(GLOB SHADER_PARTIALS CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/partials/*.hlsli")

if(NOT FXC_EXECUTABLE)
    message(WARNING
        "fxc (the HLSL compiler) was not found - using the committed "
        "shaders/CompiledShaders/*.h byte arrays as-is. Editing shaders/*.hlsl will "
        "NOT take effect until you configure from a Visual Studio / Windows SDK "
        "developer environment (or set FXC_EXECUTABLE).")
    add_custom_target(NeuronClientShaders SOURCES ${SHADER_SOURCES} ${SHADER_PARTIALS})
else()
    file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

    # Per-shader fxc defines. The lit programs bake in a fixed light count + per-vertex
    # normals; keep MAX_LIGHTS in sync with Neuron::Graphics::MAX_LIGHTS in
    # graphics/ConstantBuffers.h.
    set(SHADER_DEFINES_generic-colored-3dVS "MAX_LIGHTS=8" "NORMALS_PROVIDED=1")
    set(SHADER_DEFINES_generic-colored-3dPS "MAX_LIGHTS=8" "NORMALS_PROVIDED=1")

    set(SHADER_HEADERS "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME_WE)

        if(SHADER_NAME MATCHES "VS$")
            set(SHADER_PROFILE vs_5_0)
            set(SHADER_ENTRY VSMain)
        elseif(SHADER_NAME MATCHES "PS$")
            set(SHADER_PROFILE ps_5_0)
            set(SHADER_ENTRY PSMain)
        else()
            message(WARNING "Skipping shader with no VS/PS suffix: ${SHADER}")
            continue()
        endif()

        string(REPLACE "-" "_" SHADER_VAR "g_${SHADER_NAME}")
        set(SHADER_HEADER "${SHADER_OUT_DIR}/${SHADER_NAME}.h")

        set(SHADER_DEFINE_ARGS "")
        foreach(def ${SHADER_DEFINES_${SHADER_NAME}})
            list(APPEND SHADER_DEFINE_ARGS "/D" "${def}")
        endforeach()

        add_custom_command(
            OUTPUT "${SHADER_HEADER}"
            COMMAND "${FXC_EXECUTABLE}" /nologo /T ${SHADER_PROFILE} /E ${SHADER_ENTRY}
                    ${SHADER_DEFINE_ARGS}
                    /Fh "${SHADER_HEADER}" /Vn ${SHADER_VAR} "${SHADER}"
            DEPENDS "${SHADER}" ${SHADER_PARTIALS} "${CMAKE_CURRENT_LIST_FILE}"
            MAIN_DEPENDENCY "${SHADER}"
            COMMENT "fxc ${SHADER_NAME} -> shaders/CompiledShaders/${SHADER_NAME}.h"
            VERBATIM)

        list(APPEND SHADER_HEADERS "${SHADER_HEADER}")
    endforeach()

    add_custom_target(NeuronClientShaders DEPENDS ${SHADER_HEADERS}
        SOURCES ${SHADER_SOURCES} ${SHADER_PARTIALS})
endif()

add_dependencies(NeuronClient NeuronClientShaders)

# NeuronClient is built on top of NeuronCore; PUBLIC so the core is transitively
# available to anything that links NeuronClient.
target_link_libraries(NeuronClient PUBLIC NeuronCore)

# Windows graphics/audio/system libraries the platform backend needs. PUBLIC so
# the executable that links NeuronClient's platform objects resolves them too.
if(WIN32)
    target_link_libraries(NeuronClient PUBLIC
        d3d11
        dxgi
        d3dcompiler
        dxguid
        xaudio2
        winmm         # timing + MIDI
        comdlg32      # file dialog
        ole32         # COM / WIC
        windowscodecs # WIC image loading
    )
endif()

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(NeuronClient PRIVATE pch.h)
