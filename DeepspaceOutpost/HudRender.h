#pragma once

// The game's native 2D HUD pass - it replaced the legacy gfx2d command batch entirely.
// It draws full-window in client pixels through Neuron::Graphics::Render2D from
// GameApp::RenderCanvas (before the GUI overlay).
//
// Like the GuiWindows, this lives in a winrt/GUI translation unit so it can use Render2D /
// TextRenderer directly; it reads game state only through render-free accessors, staying
// off the legacy game headers (which define macros that don't mix with the winrt headers).
// The whole HUD moved here from the gfx_* path, and the gfx2d 2D batch is gone.
void RenderGameHud();
