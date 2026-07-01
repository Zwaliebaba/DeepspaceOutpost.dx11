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

using Neuron::Graphics::Core;

bool Renderer::initAdopt()
{
	/* Core (owned by ClientEngine) created the device/context/swap chain already. The 2D
	 * layer draws straight to Core's back-buffer RTV and Core owns presentation, so the
	 * renderer keeps no device objects of its own - it only needs Core to be up. */
	if (!Core::GetD3DDevice() || !Core::GetD3DDeviceContext() || !Core::GetSwapChain())
		return false;

	const auto size = Core::GetOutputSize();
	client_w_ = std::max<int>(1, static_cast<int>(size.Width));
	client_h_ = std::max<int>(1, static_cast<int>(size.Height));

	loadPalette();   /* non-fatal */
	return true;
}

void Renderer::shutdown()
{
	/* Core owns the device/context and clears their state in Core::Shutdown(); the
	 * renderer has nothing of its own to release. */
}

bool Renderer::loadPalette()
{
	/* The master 256-colour palette is baked into the engine (scanner_palette.h):
	 * entries are already in paletteColour()'s 0xAABBGGRR byte order; index 0 is the
	 * transparent colour key. */
	std::memcpy(palette_, kScannerPalette, sizeof(palette_));
	return true;
}

void Renderer::onResizePre()
{
	/* Unbind render targets so Core::WindowSizeChanged's ResizeBuffers can succeed
	 * (no outstanding references / bound views on the swap-chain buffers). */
	if (auto* ctx = Core::GetD3DDeviceContext())
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::onResizePost(int clientWidth, int clientHeight)
{
	client_w_ = std::max<int>(1, clientWidth);
	client_h_ = std::max<int>(1, clientHeight);
}
