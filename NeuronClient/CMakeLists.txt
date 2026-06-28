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
)

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
