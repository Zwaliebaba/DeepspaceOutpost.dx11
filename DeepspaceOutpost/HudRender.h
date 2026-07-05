#pragma once

// The game's native 2D HUD pass, replacing the legacy gfx2d command batch element by
// element. It draws full-window in client pixels through Neuron::Graphics::Render2D from
// GameApp::RenderCanvas (after gfx2d_flush, before the GUI overlay).
//
// Like the GuiWindows, this lives in a winrt/GUI translation unit so it can use Render2D /
// TextRenderer directly; it reads game state only through render-free accessors, staying
// off the legacy gfx_* / game headers (which define macros that don't mix with the winrt
// headers). As HUD elements move here from the gfx_* path, the gfx2d batch shrinks toward
// deletion.
void RenderGameHud();
