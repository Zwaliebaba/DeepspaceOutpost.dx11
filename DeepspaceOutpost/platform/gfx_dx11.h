/*
 * DeepspaceOutpost - DirectX 11 / XAudio2 port of Elite: The New Kind.
 *
 * gfx_dx11.h
 *
 * Hook for the platform layer to flush the accumulated 2D primitive batch into
 * the canvas once per presented frame (called from gfx_update_screen, before
 * Renderer::present()).
 */

#ifndef GFX_DX11_H
#define GFX_DX11_H

void gfx_dx11_flush(void);

#endif /* GFX_DX11_H */
