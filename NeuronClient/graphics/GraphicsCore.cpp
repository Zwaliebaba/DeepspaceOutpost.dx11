#include "pch.h"
#include "GraphicsCore.h"

using namespace Neuron::Graphics;

namespace
{
#if defined(_DEBUG)
  // Check for SDK Layer support.
  bool SdkLayersAvailable() noexcept
  {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION,
                                   nullptr, nullptr, nullptr);

    return SUCCEEDED(hr);
  }
#endif

  DXGI_FORMAT NoSRGB(DXGI_FORMAT fmt) noexcept
  {
    switch (fmt)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8X8_UNORM;
    default:
      return fmt;
    }
  }

  long ComputeIntersectionArea(long ax1, long ay1, long ax2, long ay2, long bx1, long by1, long bx2, long by2) noexcept
  {
    return std::max(0l, std::min(ax2, bx2) - std::max(ax1, bx1)) * std::max(0l, std::min(ay2, by2) - std::max(ay1, by1));
  }
} // namespace

// Constructor for Core.
void Core::Startup(DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthBufferFormat, UINT backBufferCount, D3D_FEATURE_LEVEL minFeatureLevel,
                   unsigned int flags) noexcept
{
  m_backBufferFormat = backBufferFormat;
  m_depthBufferFormat = depthBufferFormat;
  m_backBufferCount = backBufferCount;
  m_d3dMinFeatureLevel = minFeatureLevel;
  m_options = flags | c_FlipPresent;

  CreateDeviceResources();
}

// Configures the Direct3D device, and stores handles to it and the device context.
void Core::CreateDeviceResources()
{
  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
  if (SdkLayersAvailable())
  {
    // If the project is in a debug build, enable debugging via SDK Layers with this flag.
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
  }
  else
    DebugTrace("WARNING: Direct3D Debug Device is not available\n");
#endif

  CreateFactory();

  // Determines whether tearing support is available for fullscreen borderless windows.
  if (m_options & c_AllowTearing)
  {
    BOOL allowTearing = FALSE;
    HRESULT hr = S_OK;

    com_ptr<IDXGIFactory5> factory5;
    if (m_dxgiFactory.try_as(factory5))
      hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));

    if (FAILED(hr) || !allowTearing)
    {
      m_options &= ~c_AllowTearing;
      DebugTrace("WARNING: Variable refresh rate displays not supported");
    }
  }

  // Disable HDR if we are on an OS that can't support FLIP swap effects
  if (m_options & c_EnableHDR)
  {
    com_ptr<IDXGIFactory5> factory5;
    if (!m_dxgiFactory.try_as(factory5))
    {
      m_options &= ~c_EnableHDR;
      DebugTrace("WARNING: HDR swap chains not supported");
    }
  }

  // Disable FLIP if not on a supporting OS
  if (m_options & c_FlipPresent)
  {
    com_ptr<IDXGIFactory4> factory4;
    if (!m_dxgiFactory.try_as(factory4))
    {
      m_options &= ~c_FlipPresent;
      DebugTrace("INFO: Flip swap effects not supported");
    }
  }

  // Determine DirectX hardware feature levels this app will support.
  static constexpr D3D_FEATURE_LEVEL s_featureLevels[] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1,
  };

  UINT featLevelCount = 0;
  for (; featLevelCount < static_cast<UINT>(std::size(s_featureLevels)); ++featLevelCount)
  {
    if (s_featureLevels[featLevelCount] < m_d3dMinFeatureLevel)
      break;
  }

  if (!featLevelCount)
    throw std::out_of_range("minFeatureLevel too high");

  com_ptr<IDXGIAdapter1> adapter;
  GetHardwareAdapter(adapter.put());

  // Create the Direct3D 11 API device object and a corresponding context.
  com_ptr<ID3D11Device> device;
  com_ptr<ID3D11DeviceContext> context;

  HRESULT hr = E_FAIL;
  if (adapter)
  {
    hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, creationFlags, s_featureLevels, featLevelCount,
                           D3D11_SDK_VERSION, device.put(), // Returns the Direct3D device created.
                           &m_d3dFeatureLevel, // Returns feature level of device created.
                           context.put() // Returns the device immediate context.
      );
  }
  else
    throw std::runtime_error("No Direct3D hardware device found");

  check_hresult(hr);

#ifndef NDEBUG
  com_ptr<ID3D11Debug> d3dDebug;
  if (device.try_as(d3dDebug))
  {
    com_ptr<ID3D11InfoQueue> d3dInfoQueue;
    if (d3dDebug.try_as(d3dInfoQueue))
    {
#ifdef _DEBUG
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif
      D3D11_MESSAGE_ID hide[] = {D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,};
      D3D11_INFO_QUEUE_FILTER filter = {};
      filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
      filter.DenyList.pIDList = hide;
      d3dInfoQueue->AddStorageFilterEntries(&filter);
    }
  }
#endif

  device.as(m_d3dDevice);
  context.as(m_d3dContext);
  context.as(m_d3dAnnotation);

  // (Donor called TextureManager::Startup() here; the legacy D3D9 TextureManager is
  // not imported. The native D3D11 texture path is wired in a later phase.)
}

// These resources need to be recreated every time the window size is changed.
void Core::CreateWindowSizeDependentResources()
{
  if (!m_window)
    throw std::logic_error("Call SetWindow with a valid Win32 window handle");

  // Clear the previous window size specific context.
  m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
  m_d3dRenderTargetView = nullptr;
  m_d3dDepthStencilView = nullptr;
  m_renderTarget = nullptr;
  m_depthStencil = nullptr;
  m_d3dContext->Flush();

  // Determine the render target size in pixels.
  const UINT backBufferWidth = std::max<UINT>(static_cast<UINT>(m_outputSize.Width), 1u);
  const UINT backBufferHeight = std::max<UINT>(static_cast<UINT>(m_outputSize.Height), 1u);
  const DXGI_FORMAT backBufferFormat = (m_options & (c_FlipPresent | c_AllowTearing | c_EnableHDR))
                                         ? NoSRGB(m_backBufferFormat)
                                         : m_backBufferFormat;

  if (m_swapChain)
  {
    // If the swap chain already exists, resize it.
    HRESULT hr = m_swapChain->ResizeBuffers(m_backBufferCount, backBufferWidth, backBufferHeight, backBufferFormat,
                                            (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
      DebugTrace("Device Lost on ResizeBuffers: Reason code 0x{:08X}\n",
                static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
      // If the device was removed for any reason, a new device and swap chain will need to be created.
      HandleDeviceLost();

      // Everything is set up now. Do not continue execution of this method. HandleDeviceLost will reenter this method
      // and correctly set up the new device.
      return;
    }
    check_hresult(hr);
  }
  else
  {
    // Create a descriptor for the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = backBufferWidth;
    swapChainDesc.Height = backBufferHeight;
    swapChainDesc.Format = backBufferFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = m_backBufferCount;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = (m_options & (c_FlipPresent | c_AllowTearing | c_EnableHDR))
                                 ? DXGI_SWAP_EFFECT_FLIP_DISCARD
                                 : DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.Flags = (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
    fsSwapChainDesc.Windowed = TRUE;

    // Create a SwapChain from a Win32 window.
    check_hresult(m_dxgiFactory->CreateSwapChainForHwnd(m_d3dDevice.get(), m_window, &swapChainDesc, &fsSwapChainDesc, nullptr,
                                                        m_swapChain.put()));

    // This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
    check_hresult(m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER));
  }

  // Handle color space settings for HDR
  UpdateColorSpace();

  // Create a render target view of the swap chain back buffer.
  check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_renderTarget.put())));

  CD3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc(D3D11_RTV_DIMENSION_TEXTURE2D, m_backBufferFormat);
  check_hresult(m_d3dDevice->CreateRenderTargetView(m_renderTarget.get(), &renderTargetViewDesc, m_d3dRenderTargetView.put()));

  if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN)
  {
    // Create a depth stencil view for use with 3D rendering if needed.
    CD3D11_TEXTURE2D_DESC depthStencilDesc(m_depthBufferFormat, backBufferWidth, backBufferHeight, 1,
                                           // This depth stencil view has only one texture.
                                           1, // Use a single mipmap level.
                                           D3D11_BIND_DEPTH_STENCIL);

    check_hresult(m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, m_depthStencil.put()));

    CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
    check_hresult(m_d3dDevice->CreateDepthStencilView(m_depthStencil.get(), &depthStencilViewDesc, m_d3dDepthStencilView.put()));
  }

  // Set the 3D rendering viewport to target the entire window.
  m_screenViewport = CD3D11_VIEWPORT(0.0f, 0.0f, static_cast<float>(backBufferWidth), static_cast<float>(backBufferHeight));
}

// This method is called when the Win32 window is created (or re-created).
void Core::SetWindow(HWND window, int width, int height) noexcept
{
  m_window = window;

  m_outputSize.Width = width;
  m_outputSize.Height = height;
}

// This method is called when the Win32 window changes size
bool Core::WindowSizeChanged(int width, int height)
{
    Windows::Foundation::Size newRc;
  newRc.Width = width;
  newRc.Height = height;

  if (newRc == m_outputSize)
  {
    // Handle color space settings for HDR
    UpdateColorSpace();

    return false;
  }

  m_outputSize = newRc;
  CreateWindowSizeDependentResources();
  return true;
}

// Recreate all device resources and set them back to the current state.
void Core::HandleDeviceLost()
{
  m_d3dDepthStencilView = nullptr;
  m_d3dRenderTargetView = nullptr;
  m_renderTarget = nullptr;
  m_depthStencil = nullptr;
  m_swapChain = nullptr;
  m_d3dContext = nullptr;
  m_d3dAnnotation = nullptr;

#ifdef _DEBUG
  {
    com_ptr<ID3D11Debug> d3dDebug;
    if (m_d3dDevice.try_as(d3dDebug))
      d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY);
  }
#endif

  m_d3dDevice = nullptr;
  m_dxgiFactory = nullptr;

  CreateDeviceResources();
  CreateWindowSizeDependentResources();
}

// Present the contents of the swap chain to the screen.
void Core::Present()
{
  HRESULT hr = E_FAIL;
  if (m_options & c_AllowTearing)
  {
    // Recommended to always use tearing if supported when using a sync interval of 0.
    hr = m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
  }
  else
  {
    // The first argument instructs DXGI to block until VSync, putting the application
    // to sleep until the next VSync. This ensures we don't waste any cycles rendering
    // frames that will never be displayed to the screen.
    hr = m_swapChain->Present(1, 0);
  }

  // Discard the contents of the render target.
  // This is a valid operation only when the existing contents will be entirely
  // overwritten. If dirty or scroll rects are used, this call should be removed.
  m_d3dContext->DiscardView(m_d3dRenderTargetView.get());

  if (m_d3dDepthStencilView)
  {
    // Discard the contents of the depth stencil.
    m_d3dContext->DiscardView(m_d3dDepthStencilView.get());
  }

  // If the device was removed either by a disconnection or a driver upgrade, we
  // must recreate all device resources.
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
  {
#ifdef _DEBUG
    char buff[64] = {};
    sprintf_s(buff, "Device Lost on Present: Reason code 0x%08X\n",
              static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
    OutputDebugStringA(buff);
#endif
    HandleDeviceLost();
  }
  else
  {
    check_hresult(hr);

    if (!m_dxgiFactory->IsCurrent())
      UpdateColorSpace();
  }
}

void Core::CreateFactory()
{
#if defined(_DEBUG) && (_WIN32_WINNT >= 0x0603 /*_WIN32_WINNT_WINBLUE*/)
  bool debugDXGI = false;
  {
    com_ptr<IDXGIInfoQueue> dxgiInfoQueue;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.put()))))
    {
      debugDXGI = true;

      check_hresult(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(m_dxgiFactory.put())));

      dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
      dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

      DXGI_INFO_QUEUE_MESSAGE_ID hide[] = {
        80 /* IDXGISwapChain::GetContainingOutput: The swapchain's adapter does not control the output on which the swapchain's window
                    resides. */
        ,
      };
      DXGI_INFO_QUEUE_FILTER filter = {};
      filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
      filter.DenyList.pIDList = hide;
      dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
    }
  }

  if (!debugDXGI)
#endif

    check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(m_dxgiFactory.put())));
}

// This method acquires the first available hardware adapter.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
void Core::GetHardwareAdapter(IDXGIAdapter1** ppAdapter)
{
  *ppAdapter = nullptr;

  com_ptr<IDXGIAdapter1> adapter;

  com_ptr<IDXGIFactory6> factory6;
  m_dxgiFactory.try_as(factory6);

  for (UINT adapterIndex = 0; SUCCEEDED(
         factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.put())));
       adapterIndex++)
  {
    DXGI_ADAPTER_DESC1 desc;
    check_hresult(adapter->GetDesc1(&desc));

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
    {
      // Don't select the Basic Render Driver adapter.
      continue;
    }

    DebugTrace(L"Direct3D Adapter ({}): VID:{:04X}, PID:{:04X} - {}\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);

    break;
  }

  if (!adapter)
  {
    for (UINT adapterIndex = 0; SUCCEEDED(m_dxgiFactory->EnumAdapters1(adapterIndex, adapter.put())); adapterIndex++)
    {
      DXGI_ADAPTER_DESC1 desc;
      check_hresult(adapter->GetDesc1(&desc));

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      {
        // Don't select the Basic Render Driver adapter.
        continue;
      }

      DebugTrace(L"Direct3D Adapter ({}): VID:{:04X}, PID:{:04X} - {}\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
      break;
    }
  }

  *ppAdapter = adapter.detach();
}

// Sets the color space for the swap chain in order to handle HDR output.
void Core::UpdateColorSpace()
{
  if (!m_dxgiFactory)
    return;

  if (!m_dxgiFactory->IsCurrent())
  {
    // Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
    CreateFactory();
  }

  DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

  bool isDisplayHDR10 = false;

  if (m_swapChain)
  {
    // To detect HDR support, we will need to check the color space in the primary
    // DXGI output associated with the app at this point in time
    // (using window/display intersection).

    // Get the retangle bounds of the app window.
    RECT windowBounds;
    if (!GetWindowRect(m_window, &windowBounds))
      throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "GetWindowRect");

    const long ax1 = windowBounds.left;
    const long ay1 = windowBounds.top;
    const long ax2 = windowBounds.right;
    const long ay2 = windowBounds.bottom;

    com_ptr<IDXGIOutput> bestOutput;
    long bestIntersectArea = -1;

    com_ptr<IDXGIAdapter> adapter;
    for (UINT adapterIndex = 0; SUCCEEDED(m_dxgiFactory->EnumAdapters(adapterIndex, adapter.put())); ++adapterIndex)
    {
      com_ptr<IDXGIOutput> output;
      for (UINT outputIndex = 0; SUCCEEDED(adapter->EnumOutputs(outputIndex, output.put())); ++outputIndex)
      {
        // Get the rectangle bounds of current output.
        DXGI_OUTPUT_DESC desc;
        check_hresult(output->GetDesc(&desc));
        const auto& r = desc.DesktopCoordinates;

        // Compute the intersection
        const long intersectArea = ComputeIntersectionArea(ax1, ay1, ax2, ay2, r.left, r.top, r.right, r.bottom);
        if (intersectArea > bestIntersectArea)
        {
          std::swap(bestOutput, output);
          bestIntersectArea = intersectArea;
        }
      }
    }

    if (bestOutput)
    {
      if (bestOutput.try_as(m_dxgiOutput))
      {
        DXGI_OUTPUT_DESC1 desc;
        check_hresult(m_dxgiOutput->GetDesc1(&desc));

        if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
        {
          // Display output is HDR10.
          isDisplayHDR10 = true;
        }
      }
    }
  }

  if ((m_options & c_EnableHDR) && isDisplayHDR10)
  {
    switch (m_backBufferFormat)
    {
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      // The application creates the HDR10 signal.
      colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
      break;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      // The system creates the HDR10 signal; application uses linear values.
      colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
      break;

    default:
      break;
    }
  }

  m_colorSpace = colorSpace;

  if (m_swapChain)
  {
    com_ptr<IDXGISwapChain3> swapChain3;
    m_swapChain.as(swapChain3);

    UINT colorSpaceSupport = 0;
    if (SUCCEEDED(swapChain3->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport)) && (colorSpaceSupport &
      DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
      check_hresult(swapChain3->SetColorSpace1(colorSpace));
  }
}

Windows::Foundation::Size Core::GetScreenSize()
{
  // Enumerate all adapters to find one with outputs (handles hybrid GPU laptops where
  // the discrete GPU used for rendering may not have any attached display outputs)
  com_ptr<IDXGIAdapter> adapter;
  for (UINT adapterIndex = 0; SUCCEEDED(m_dxgiFactory->EnumAdapters(adapterIndex, adapter.put())); ++adapterIndex)
  {
    com_ptr<IDXGIOutput> output;
    if (SUCCEEDED(adapter->EnumOutputs(0, output.put())))
    {
      DXGI_OUTPUT_DESC desc;
      check_hresult(output->GetDesc(&desc));
      return {
        static_cast<float>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left),
        static_cast<float>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top)
      };
    }
  }

  // Fallback to Win32 API if no adapter with outputs is found
  return { static_cast<float>(GetSystemMetrics(SM_CXSCREEN)), static_cast<float>(GetSystemMetrics(SM_CYSCREEN)) };
}
