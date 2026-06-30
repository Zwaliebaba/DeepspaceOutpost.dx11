#include "pch.h"
#include "ClientEngine.h"

#include "Canvas.h"
#include "GuiOverlay.h"
#include "Render2D.h"
#include "Strings.h"
#include "TextRenderer.h"
#include "TextureManager.h"

#include "EventManager.h"
#include "Renderer.h"
#include "input_win.h"
#include "platform_win.h" // platform_pump_messages

#include "gfx.h"    // gfx_set_scene_fullwindow
#include "gfx2d.h"  // gfx2d_flush

namespace
{
  const wchar_t* kWindowClass = L"DeepspaceOutpostWindow";

  // Initial client size: the game is authored against a 512x514 canvas, presented at
  // an integer multiple. Matches the size the previous bespoke platform window used.
  constexpr int kClientWidth = 1024;
  constexpr int kClientHeight = 1026;

  // The engine window procedure. Routes messages to registered processors via
  // EventManager (input, MIDI loop, ...); WM_DESTROY ends the message loop and WM_SIZE
  // drives the swap-chain + renderer resize via ClientEngine::OnResize.
  LRESULT CALLBACK EngineWndProc(HWND _hWnd, UINT _msg, WPARAM _wParam, LPARAM _lParam)
  {
    if (_msg == WM_DESTROY)
    {
      PostQuitMessage(0);
      return 0;
    }

    if (_msg == WM_SIZE)
    {
      if (_wParam != SIZE_MINIMIZED)
        Neuron::Client::ClientEngine::OnResize(LOWORD(_lParam), HIWORD(_lParam));
      return 0;
    }

    if (EventManager::WndProc(_hWnd, _msg, _wParam, _lParam) == -1)
      return DefWindowProcW(_hWnd, _msg, _wParam, _lParam);
    return 0;
  }
}

namespace Neuron::Client
{
  void ClientEngine::Startup(const wchar_t* _gameName, HINSTANCE _hInstance, int _nCmdShow)
  {
    CoreEngine::Startup();
    Strings::Startup();

    // Bring up the native D3D11 device (2D overlay + canvas: no depth buffer).
    Graphics::Core::Startup(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN);

    m_instance = _hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = EngineWndProc;
    wc.hInstance = _hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    RECT rc{0, 0, kClientWidth, kClientHeight};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rc, style, FALSE);

    m_hwnd = CreateWindowExW(0, kWindowClass, _gameName, style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, _hInstance, nullptr);

    ShowWindow(m_hwnd, _nCmdShow);
    UpdateWindow(m_hwnd);

    RECT client{};
    GetClientRect(m_hwnd, &client);
    Graphics::Core::SetWindow(m_hwnd, client.right - client.left, client.bottom - client.top);
    Graphics::Core::CreateWindowSizeDependentResources();
    Graphics::Render2D::Startup();

    Canvas::Startup();
    g_gameFont.Startup("Fonts/SpeccyFontENG.dds");
    g_editorFont.Startup("Fonts/SpeccyFontENG.dds");

    input_register_event_processor();
    GuiOverlay::Startup();

    m_windowReady = true; // device + swap chain are up; WM_SIZE may now rebuild them
  }

  void ClientEngine::OnResize(int _width, int _height)
  {
    if (!m_windowReady || _width <= 0 || _height <= 0)
      return;

    // Release our back-buffer view, resize the Core swap chain, then rebuild it. The
    // Renderer may not be adopted yet (it comes up in gfx_graphics_startup); guard it.
    Renderer* r = platform_renderer();
    if (r)
      r->onResizePre();

    Graphics::Core::WindowSizeChanged(_width, _height);

    if (r)
      r->onResizePost(_width, _height);

    // Render2D holds no window-size-dependent state (it takes the target + size at
    // Begin), so there is nothing to rebuild here.

    if (m_main)
      m_main->OnWindowSizeChanged(_width, _height);
  }

  void ClientEngine::StartGame(const winrt::com_ptr<GameMain>& _gameMain)
  {
    m_main = _gameMain;
    if (m_main)
      m_main->Startup();
  }

  void ClientEngine::Frame(int frameCapMs)
  {
    const int capMs = (frameCapMs > 0) ? frameCapMs : 55;

    // Render + present through the GameMain lifecycle. Guarded against re-entrancy: the
    // classic game drives nested blocking sequences (the break pattern, mission briefs)
    // that call gfx_update_screen() -> Frame() from inside the frame. Those nested frames
    // must only present the inner sequence's drawing, not re-run this frame's
    // Update/RenderScene (which would recurse / draw the flight scene over them) - but
    // they still pump + pace below.
    if (m_main)
    {
      static bool s_inLifecycle = false;
      if (!s_inLifecycle)
      {
        s_inLifecycle = true;

        // Per-frame logic hook (GameApp::Update -> game_update): network, sound, input,
        // bookkeeping. dt is the fixed timestep the game is paced to. Inert outside the
        // in-flight/docked loop.
        m_main->Update(capMs / 1000.0f);

        // Scene hook (GameApp::RenderScene -> game_render_scene): draw the 3D + HUD into
        // the 2D batch before the flush so it composites correctly. Inert outside that
        // loop, in which case the active screen's own loop already filled the batch.
        m_main->RenderScene();

        s_inLifecycle = false;
      }

      // Replay the frame's 2D batch (letterboxed) to the back buffer. An idle frame (empty
      // batch, overlay hidden) paints nothing and is not presented, so the last presented
      // frame stays on screen. The overlay forces a fresh cleared frame to composite onto.
      GuiOverlay::Update();
      const bool overlayShown = GuiOverlay::IsShown();
      const bool painted = gfx2d_flush(overlayShown);
      if (painted)
      {
        m_main->RenderCanvas(); // 2D UI (GUI overlay) on top of the scene
        if (Renderer* r = platform_renderer())
          r->swap();
      }

      // Default the NEXT frame to the retro letterboxed canvas; the in-flight render path
      // re-enables full-window mode each frame it draws.
      gfx_set_scene_fullwindow(0);
    }

    // Drain OS messages (terminates the process if the window closed), then regulate to
    // capMs per frame so the game runs at the intended pace.
    platform_pump_messages();

    static LARGE_INTEGER s_freq = {};
    static LARGE_INTEGER s_prev = {};
    if (s_freq.QuadPart == 0)
      QueryPerformanceFrequency(&s_freq);

    const double target = capMs / 1000.0;
    if (s_prev.QuadPart != 0)
    {
      for (;;)
      {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double elapsed = double(now.QuadPart - s_prev.QuadPart) / double(s_freq.QuadPart);
        if (elapsed >= target)
          break;
        if (target - elapsed > 0.003)
          Sleep(1);
      }
    }
    QueryPerformanceCounter(&s_prev);
  }

  void ClientEngine::Shutdown()
  {
    if (m_main)
    {
      m_main->Shutdown();
      m_main = nullptr;
    }

    GuiOverlay::Shutdown();
    g_editorFont.Shutdown();
    g_gameFont.Shutdown();
    Graphics::TextureManager::Shutdown();
    Canvas::Shutdown();
    Graphics::Render2D::Shutdown();
    Graphics::Core::Shutdown();
    Strings::Shutdown();
    CoreEngine::Shutdown();
  }
}
