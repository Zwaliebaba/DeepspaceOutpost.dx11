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

    # Engine front door: window + native D3D11 device + GameMain dispatch.
    ClientEngine.cpp

    # Render seam + generic client engine (portable C++; no Win32/D3D).
    RenderQueue.cpp
    RenderContext.cpp
    ReplicationClient.cpp
    Strings.cpp
    vector.cpp

    # Native Direct3D 11 graphics core + immediate-mode renderer (Neuron::Graphics).
    # Imported from the donor NeuronClient; the foundation the GUI/text/menu import
    # is built on. See docs/phase1-graphicscore.md.
    graphics/GraphicsCore.cpp
    graphics/Render2D.cpp
    graphics/TextureManager.cpp
    graphics/DDSTextureLoader.cpp

    # Imported text + GUI menu system (Phases 3-4). Drawn through Neuron::Graphics::Render2D;
    # menu navigation reads keyboard.h directly and the exit button calls
    # platform_request_quit(). Textures load directly through
    # Neuron::Graphics::TextureManager. See docs/phase2-4-gui-text.md.
    gui/TextRenderer.cpp
    gui/GuiButton.cpp
    gui/GuiWindow.cpp
    gui/Canvas.cpp
    gui/GuiOverlay.cpp

    # Win32 / Direct3D 11 / XAudio2 platform backend.
    platform/platform_win.cpp
    platform/Renderer.cpp
    platform/gfx2d.cpp
    platform/GfxRenderSink.cpp
    platform/audio_xa2.cpp
    platform/input_win.cpp
)

set(NEURONCLIENT_HEADERS
    NeuronClient.h
    pch.h
    ClientEngine.h
    GameMain.h

    # Native Direct3D 11 graphics core headers.
    graphics/GraphicsCore.h
    graphics/Render2D.h
    graphics/TextureManager.h
    graphics/DDSTextureLoader.h

    # Imported text + GUI menu system headers.
    gui/TextRenderer.h
    gui/Widget.h
    gui/GuiButton.h
    gui/GuiWindow.h
    gui/Canvas.h
    gui/GuiOverlay.h

    # Generic engine headers.
    Strings.h
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

    # Platform backend headers.
    platform/Renderer.h
    platform/gfx2d.h
    platform/scanner_palette.h
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

# The 2D renderer (Neuron::Graphics::Render2D) and the present blit compile their
# shaders at runtime via D3DCompile (d3dcompiler), so there is no offline fxc step
# or committed shader byte-array headers to build.

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
