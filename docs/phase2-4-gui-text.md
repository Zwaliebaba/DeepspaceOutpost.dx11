# GUI / Text / GraphicsCore import — Phases 2–4

Builds on Phase 1 (`docs/phase1-graphicscore.md`). Like Phase 1 this is **additive
and not yet wired into the live frame** — none of it runs until the Phase 5 device
unification initialises `Neuron::Graphics::Core` and routes the present path through
`ImmediateRenderer`. **Not compiled/run here** (Linux, no MSVC/DX11); needs a Windows
build to verify.

## Phase 2 — native D3D11 texture manager

`NeuronClient/graphics/TextureManager.{h,cpp}` — `Neuron::Graphics::Texture` /
`TextureManager`.

- Every `.dds` in `GameData` is **uncompressed 32-bpp** (verified: `pfFlags=0x41`,
  no fourCC — no DXTn/BCn). So instead of porting the donor's 859-line **D3D9**
  `DDSTextureLoader` (which also dragged in the hard-coded June-2010 DirectX SDK),
  this loads through the existing `platform/Image.h::load_image_rgba` and creates an
  immutable `ID3D11Texture2D` + SRV from `Core::GetD3DDevice()`. Native, Windows-SDK
  only, Native-First.
- `LoadTexture(name)` caches by name and always returns a non-null `Texture`; if the
  device isn't up yet or the file is missing, the texture is simply "not loaded".

## Phase 3 — text rendering + Strings

- `NeuronClient/gui/TextRenderer.{h,cpp}` — the donor's bitmap-font renderer (it was
  named `DX9TextRenderer` in the donor),
  **trimmed to the 2D screen-space path** the menus use. The world-space `DrawText3D*`
  overloads (which still rode the legacy `gl*` path + camera/app) are omitted, and
  the coroutine/`ASyncLoader` async-load machinery is replaced with a synchronous
  `TextureManager::LoadTexture`. Renders through the `TextOverlay` program. Exposes
  `g_gameFont` / `g_editorFont`. Point it at a `GameData/Fonts/*.dds` sheet at startup.
- `NeuronClient/gui/Strings.{h,cpp}` — JSON string table, adapted to the target:
  reads `GameData/Strings/<lang>/<class>.json` (CWD-relative, staged with GameData)
  via `std::ifstream` + `Neuron::Json`, with **no WinRT MRT / Globalization**
  dependency (donor used `Windows::Globalization`). Defaults to `en-US`.
- `GameData/Strings/en-US/Strings.json` — a starter table.

## Phase 4 — GuiWindow / GuiButton / Canvas

Ported `Widget`, `GuiButton` (+ `BorderlessButton`/`CloseButton`/`GameExitButton`/
`InvertedBox`/`LabelButton`), `GuiWindow`, and `Canvas` (the ECL window manager)
into `NeuronClient/gui/`, drawing through `ImmediateRenderer` + `TextRenderer`.

The donor's heavy dependency web was replaced with **thin native shims** rather than
imported wholesale (Native-First; the target already has its own input/filesystem):

| Donor dependency | Replaced with |
|---|---|
| `InputManager` + ControlBindings/driver stack | `GuiWindow::Update` reads `keyboard.h` state directly (`kbd_up/down/enter/escape_pressed`) |
| `Resource` (Bitmap/Shape/Sound/...) | direct `Neuron::Graphics::TextureManager::LoadTexture(name)->GetShaderResourceView()` (no wrapper; `LoadTexture` takes a `std::string`) |
| `GameApp` singleton | `gui/GameApp.h` — just `g_app->m_requestQuit` |
| `ClientEngine::OutputSize()` | direct `Neuron::Graphics::Core::GetOutputSize()` calls (no wrapper) |
| `darwiniaRandom()` | `rand()` (cosmetic window-placement jitter only) |
| `Timer::Core::GetTotalSeconds()` | the target's own `Neuron::Timer::Core` (kept) |

### Deliberately deferred
- **`InputField` / `InputScroller`** (numeric/string value-slider widgets) are NOT
  imported: they still used raw `gl*` calls, `GetHighResTime`, `Keydefs`, and a
  cursor stack. The two `GuiWindow` methods that used them
  (`CreateValueControl`/`RemoveValueControl`) were dropped. Port `InputField` onto
  `ImmediateRenderer` later if value controls are needed.

### Known limitations to verify on Windows
1. **Nothing renders until Phase 5** (Core isn't initialised; `TextureManager` /
   `TextRenderer` need `Core::GetD3DDevice()`).
2. Menu input is **held-key state, not edge-triggered** (`GuiWindow::Update` reads
   `kbd_*_pressed` directly); menu navigation may repeat fast. Add edge detection when
   wiring the GUI into the main loop.
3. **Coordinate space**: the GUI assumes screen/client pixels via
   `Neuron::Graphics::Core::GetOutputSize()`; the legacy game draws into a letterboxed 512×514
   canvas. Decide the GUI's space when hooking `Canvas::Render()`/`EclUpdate()` into
   the frame (recommended: draw the GUI in client space on top, after the canvas blit).
4. `Canvas` must be driven from the main loop (mouse/keyboard feed +
   `EclUpdate()`/`Render()`), and `g_gameFont`/`g_editorFont`/`Strings::Startup()`
   initialised once the device is up — all part of Phase 5.

## Running plan
- Phase 1: GraphicsCore + ImmediateRenderer + shaders. ✅
- Phase 2: native TextureManager + DDS. ✅
- Phase 3: text renderer + Strings. ✅
- Phase 4: GuiWindow/GuiButton/Canvas (+ shims). ✅
- **Phase 5 (next):** device unification + present-path swap; init Core /
  ImmediateRenderer / fonts / Strings; drive Canvas from the main loop; reimplement
  the `gfx_*` contract on ImmediateRenderer; retire `Renderer`/`gfx_dx11`/`Font`;
  convert one real menu to a `GuiWindow` end-to-end.
