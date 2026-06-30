/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Renderer.cpp
 */

#include "pch.h"

#include "Renderer.h"
#include "GraphicsCore.h"    // Neuron::Graphics::Core (device unification)
#include "scanner_palette.h" // baked master 256-colour palette

#include <algorithm>
#include <cstring>

using winrt::com_ptr;

bool Renderer::initAdopt()
{
	using namespace Neuron::Graphics;

	/* Take references on the device/context/swap chain Core created (ClientEngine owns
	 * the lifetime). The 2D layer draws straight to Core's back-buffer RTV, so the
	 * renderer holds no render targets of its own. */
	device_.copy_from(Core::GetD3DDevice());
	context_.copy_from(Core::GetD3DDeviceContext());
	swap_chain_.copy_from(Core::GetSwapChain());
	if (!device_ || !context_ || !swap_chain_) return false;

	hwnd_ = Core::GetWindow();
	const auto size = Core::GetOutputSize();
	client_w_ = std::max<int>(1, static_cast<int>(size.Width));
	client_h_ = std::max<int>(1, static_cast<int>(size.Height));

	loadPalette();   /* non-fatal */
	return true;
}

void Renderer::shutdown()
{
	if (context_) context_->ClearState();
	context_ = nullptr;
	swap_chain_ = nullptr;
	device_ = nullptr;
}

bool Renderer::loadPalette()
{
	/* The master 256-colour palette is baked into the engine (scanner_palette.h):
	 * entries are already in paletteColour()'s 0xAABBGGRR byte order; index 0 is the
	 * transparent colour key. */
	std::memcpy(palette_, kScannerPalette, sizeof(palette_));
	palette_loaded_ = true;
	return true;
}

void Renderer::swap()
{
	if (swap_chain_) swap_chain_->Present(1, 0);
}

void Renderer::onResizePre()
{
	/* Unbind render targets so Core::WindowSizeChanged's ResizeBuffers can succeed
	 * (no outstanding references / bound views on the swap-chain buffers). */
	if (context_) context_->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::onResizePost(int clientWidth, int clientHeight)
{
	client_w_ = std::max<int>(1, clientWidth);
	client_h_ = std::max<int>(1, clientHeight);
}
