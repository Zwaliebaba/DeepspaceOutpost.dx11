#pragma once

// Native Direct3D 11 device core (Neuron::Graphics::Core).
//
// Imported from the donor NeuronClient and adapted to the DeepspaceOutpost.dx11
// "Native-First" rule: the donor pulled in the legacy D3D9 TextureManager via this
// header and the hard-coded June-2010 DirectX SDK through DirectXHelper.h. Neither
// is used by Core, so this build includes only the native Windows-SDK D3D11/DXGI
// headers directly. com_ptr / check_hresult / Windows::Foundation come from the
// precompiled header (winrt/base.h); XM* types from GameMath.h.
#include <d3d11_4.h>
#include <dxgi1_6.h>
#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

namespace Neuron::Graphics
{
  // Controls all the DirectX device resources.
  class Core
  {
    public:
      static constexpr unsigned int c_FlipPresent = 0x1;
      static constexpr unsigned int c_AllowTearing = 0x2;
      static constexpr unsigned int c_EnableHDR = 0x4;

      static void Startup(DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT,
                      UINT backBufferCount = 2, D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_10_0,
                      unsigned int flags = c_FlipPresent) noexcept;
      static void Shutdown() {}

      static void CreateDeviceResources();
      static void CreateWindowSizeDependentResources();
      static void SetWindow(HWND window, int width, int height) noexcept;
      static bool WindowSizeChanged(int width, int height);
      static void HandleDeviceLost();
      static void Present();
      static void UpdateColorSpace();

      // Device Accessors.
      static Windows::Foundation::Size GetOutputSize() noexcept { return m_outputSize; }
      static Windows::Foundation::Size GetScreenSize();

      // Direct3D Accessors.
      static auto GetD3DDevice() noexcept { return m_d3dDevice.get(); }
      static auto GetD3DDeviceContext() noexcept { return m_d3dContext.get(); }
      static auto GetSwapChain() noexcept { return m_swapChain.get(); }
      static auto GetDXGIFactory() noexcept { return m_dxgiFactory.get(); }
      static HWND GetWindow() noexcept { return m_window; }
      static D3D_FEATURE_LEVEL GetDeviceFeatureLevel() noexcept { return m_d3dFeatureLevel; }
      static ID3D11Texture2D* GetRenderTarget() noexcept { return m_renderTarget.get(); }
      static ID3D11Texture2D* GetDepthStencil() noexcept { return m_depthStencil.get(); }
      static ID3D11RenderTargetView* GetRenderTargetView() noexcept { return m_d3dRenderTargetView.get(); }
      static ID3D11DepthStencilView* GetDepthStencilView() noexcept { return m_d3dDepthStencilView.get(); }
      static DXGI_FORMAT GetBackBufferFormat() noexcept { return m_backBufferFormat; }
      static DXGI_FORMAT GetDepthBufferFormat() noexcept { return m_depthBufferFormat; }
      static D3D11_VIEWPORT GetScreenViewport() noexcept { return m_screenViewport; }
      static UINT GetBackBufferCount() noexcept { return m_backBufferCount; }
      static DXGI_COLOR_SPACE_TYPE GetColorSpace() noexcept { return m_colorSpace; }
      static unsigned int GetDeviceOptions() noexcept { return m_options; }

      // Performance events
      static void PIXBeginEvent(_In_z_ const wchar_t* name) { m_d3dAnnotation->BeginEvent(name); }
      static void PIXEndEvent() { m_d3dAnnotation->EndEvent(); }
      static void PIXSetMarker(_In_z_ const wchar_t* name) { m_d3dAnnotation->SetMarker(name); }

    private:
      static void CreateFactory();
      static void GetHardwareAdapter(IDXGIAdapter1** ppAdapter);

      // Direct3D objects.
      inline static com_ptr<IDXGIFactory2> m_dxgiFactory;
      inline static com_ptr<IDXGIOutput6> m_dxgiOutput;

      inline static com_ptr<ID3D11Device1> m_d3dDevice;
      inline static com_ptr<ID3D11DeviceContext1> m_d3dContext;
      inline static com_ptr<IDXGISwapChain1> m_swapChain;
      inline static com_ptr<ID3DUserDefinedAnnotation> m_d3dAnnotation;

      // Direct3D rendering objects. Required for 3D.
      inline static com_ptr<ID3D11Texture2D> m_renderTarget;
      inline static com_ptr<ID3D11Texture2D> m_depthStencil;
      inline static com_ptr<ID3D11RenderTargetView> m_d3dRenderTargetView;
      inline static com_ptr<ID3D11DepthStencilView> m_d3dDepthStencilView;
      inline static D3D11_VIEWPORT m_screenViewport;

      // Direct3D properties.
      inline static DXGI_FORMAT m_backBufferFormat;
      inline static DXGI_FORMAT m_depthBufferFormat;
      inline static UINT m_backBufferCount;
      inline static D3D_FEATURE_LEVEL m_d3dMinFeatureLevel;

      // Cached device properties.
      inline static HWND m_window;
      inline static D3D_FEATURE_LEVEL m_d3dFeatureLevel = D3D_FEATURE_LEVEL_9_1;
      inline static Windows::Foundation::Size m_outputSize = {};

      // HDR Support
      inline static DXGI_COLOR_SPACE_TYPE m_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

      // Core options (see flags above)
      inline static unsigned int m_options;
  };
} 
