/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx2d.h
 *
 * Hook for the platform layer to flush the accumulated 2D primitive batch into
 * the canvas once per presented frame (called from gfx_update_screen, before
 * Renderer::present()). The batch is replayed through Neuron::Graphics::ImmediateRenderer.
 */

#ifndef GFX2D_H
#define GFX2D_H

void gfx2d_flush(void);

#endif /* GFX2D_H */
