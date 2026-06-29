# GUI / Text / GraphicsCore import — Phase 1 (foundation)

This is the first phase of importing, from the donor `NeuronClient` (the more
complete client this project's `AGENTS.md` already names as its target
architecture), three things requested:

1. the **GraphicsCore** native Direct3D 11 stack (`Neuron::Graphics::Core` +
   `ImmediateRenderer`) — to standardize the renderer on it;
2. the **text/Strings** rendering path; and
3. the **GuiWindow / GuiButton / Canvas** menu GUI.

Phases 2–5 (texture/DDS, text + `Strings`, GUI widgets + `Canvas`, and the
backend swap) build on this one. See the bottom of this file for the running
plan.

## What Phase 1 landed

Imported and build-wired the load-bearing renderer foundation. It is **additive**:
nothing about the existing `Renderer` / `gfx_dx11` / `Font` path changed yet, so
the game keeps running exactly as before while the new stack compiles alongside it.

| Added | From | Notes |
|---|---|---|
| `NeuronClient/shaders/*.hlsl` + `partials/*.hlsli` | donor `NeuronGame` | The 6 immediate-mode programs (generic colored/textured, colored-3d lit, text, text-overlay, gui-window). |
| `NeuronClient/shaders/CompiledShaders/*.h` | donor `NeuronGame` | `fxc /Fh /Vn` byte arrays (`g_<name>`), committed so the build works without a dev-env fxc. |
| `NeuronClient/graphics/GraphicsCore.{h,cpp}` | donor `NeuronClient` | Device / swap chain / RTV+DSV / present. **Adapted** — see below. |
| `NeuronClient/graphics/ImmediateRenderer.{h,cpp}` | donor `NeuronClient` | glBegin/glVertex-style immediate renderer over D3D11; `#include`s the compiled shaders. Imported verbatim. |
| `NeuronClient/graphics/ConstantBuffers.h` | donor `NeuronClient` | GPU constant-buffer ABI (b0/b3/b6/b7/b8/b9); `static_assert`-guarded. Verbatim. |

### Build wiring
> Note: the shaders originally landed in a separate `NeuronGame` library and were
> later moved into `NeuronClient/shaders/` (the `NeuronGame` library was removed to
> keep the libraries clean). The description below reflects the current layout.
- `NeuronClient/CMakeLists.txt`: new sources/headers; `graphics/` on the public
  include path; `shaders/CompiledShaders` on the private include path; an inline
  `fxc` shader-compile step producing a `NeuronClientShaders` target with
  `add_dependencies(NeuronClient NeuronClientShaders)`.
- The shader step: relaxed the "fxc not found" case from `FATAL_ERROR`
  to a warning + no-op target, since the compiled headers are committed (a clean
  configure without the Windows SDK dev-env still builds against them).

## Adaptations (why this isn't a verbatim copy)

The donor's GraphicsCore stack could **not** be copied verbatim without violating
this repo's Native-First rule (`.github/coding-standards.md`) and its
Windows-SDK-only dependency policy:

- **Dropped the legacy D3D9 path.** `GraphicsCore.h` included the donor's
  `TextureManager.h`, whose `Texture` holds a `com_ptr<IDirect3DTexture9>`, and
  the donor's `DDSTextureLoader` / `DirectXHelper.h` pull in **D3D9 + d3dx9 from a
  hard-coded `C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)` path**.
  `Core` itself uses none of that, so `GraphicsCore.h` now includes only the
  native `<d3d11_4.h>` / `<dxgi1_6.h>` (+ `<dxgidebug.h>` in debug), and the
  `TextureManager::Startup()` call in `CreateDeviceResources()` was removed. The
  native D3D11 texture/DDS path is Phase 2.
- **`DirectXMatrixStack` not imported.** `ImmediateRenderer` keeps its own
  `std::vector<XMMATRIX>` matrix stacks; the donor's D3D9-flavored
  `OpenGLD3D::DirectXMatrixStack` is unused.
- **No new wrappers.** `com_ptr` / `check_hresult` / `Windows::Foundation` come
  straight from the PCH (`winrt/base.h`); `XM*` from `GameMath.h`.

## NOT yet done — and the load-bearing risk

**This foundation has not been compiled or run.** This working environment is
Linux with no MSVC / Windows SDK / DirectX, so a Windows build is required to
verify it. Highest-risk items to check first on Windows:

1. It builds: `NeuronClientShaders` runs (or the committed headers are picked up),
   and `ImmediateRenderer.cpp` resolves all 12 `g_*` byte arrays.
2. `ConstantBuffers.h` `static_assert`s hold under this toolchain.

### The big-bang swap is deferred (next, and the actual "standardize" step)
Phase 1 deliberately does **not** initialize `Neuron::Graphics::Core` in
`WinMain` / the platform layer. `Core` creates its own swap chain on the HWND,
and the existing `Renderer` already owns one — running both would fight over the
window. Unifying device ownership and routing the present path through
`Core` + `ImmediateRenderer` (then retiring `Renderer` / `gfx_dx11.cpp` /
`platform/Font.cpp`) is the next phase, done as one focused change so it can be
built and bring-up-tested (the `ImmediateRenderer::SetSmokeTestEnabled(true)`
triangle) on Windows.

## Running plan
- **Phase 1 (this):** GraphicsCore + ImmediateRenderer + shaders, build-wired. ✅
- **Phase 2:** native D3D11 `TextureManager` + DDS loader (full DXTn/BCn);
  point `GameData/Textures/*.dds` + `GameData/Fonts/*.dds` through it.
- **Phase 3:** text renderer (donor `DX9TextRenderer`, on a `.dds` font sheet) +
  `Strings` JSON localization (`GameData/Strings/<lang>/*.json`).
- **Phase 4:** `Widget` / `GuiButton` / `GuiWindow` / `Canvas`, with input/app
  globals adapted to the target rather than importing `GameApp` wholesale.
- **Phase 5:** device unification + present-path swap; reimplement the `gfx_*`
  contract on `ImmediateRenderer`; retire `Renderer` / `gfx_dx11` / `Font`;
  convert one real menu to a `GuiWindow` end-to-end.
