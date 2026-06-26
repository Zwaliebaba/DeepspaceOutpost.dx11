# AGENTS.md

## Project Overview

Earthrise is a Windows-native C++ game built with CMake and the MSVC toolchain. It is a
restructured port of Introversion's **Darwinia**: the original game/engine code has been
split into reusable `Neuron*` engine static libraries plus the game logic and two
executables (a Win32 GUI client and a console server stub). It targets Windows (x64 and
x86) and builds with the Ninja generator via CMake presets. The renderer is **Direct3D 11**,
reached through a legacy OpenGL-over-Direct3D compatibility layer that is being migrated to
native D3D11. Vector/matrix math is being migrated from the original Darwinia math types to
**DirectXMath** (SIMD) via `Neuron::Math`.

> **Status note.** The codebase is mid-restructure. Several engineering conventions below
> (DirectXMath SIMD boundary, `winrt::com_ptr`, native-first) describe the *target* style for
> new and migrated code; large parts of the original Darwinia code still use legacy patterns.
> Match the file you are editing, and move legacy code toward the target style when you touch it.

**Current project structure (CMake):**

| Project | Type | Role |
|---|---|---|
| **NeuronCore** | Static lib | Engine foundation: math (`GameMath`/`Neuron::Math` over DirectXMath, plus legacy `LegacyVector2/3`, `Matrix33/34`, `MathCommon`), networking (`NetLib`, sockets, packets, connections), filesystem (`FileSys`, `FilePaths`), tasks/threads (`TasksCore`), timers, events (`EventManager`), debug, data serialization (`DataReader`/`DataWriter`, `BinaryStreamReaders`, `TextStreamReaders`, `Json`). Pulls in C++/WinRT projections via `NeuronCore.h`. |
| **NeuronClient** | Static lib | Client engine: Direct3D 11 graphics core (`GraphicsCore`, `TextureManager`), the `OpenglDirectx` GL-over-D3D compatibility layer, audio/sound system, input drivers, GUI/window manager, preferences. Depends on NeuronCore. |
| **NeuronServer** | Static lib | Server engine library (currently minimal — `NeuronServer.h`). Depends on NeuronCore. |
| **GameLogic** | Static lib | Darwinia game simulation **and** its rendering: entities (`Darwinian`, `Armyant`, `Centipede`, `Souldestroyer`, …), buildings, AI, particle systems, `Landscape` + `LandscapeRenderer`, routing/obstruction grids. Depends on **NeuronClient** (so it is client-linked, not server-only). |
| **EarthRise** | Win32 GUI executable | Game client: `GameApp`, main loop, `CameraController`, `Renderer`, `LevelFile`, `Location`, `Team`, user input. Entry point `wWinMain` (`WinMain.cpp`). Links GameLogic. |
| **Server** | Console executable | Dedicated-server stub (`Main.cpp`, entry point `main`). Links NeuronServer. |

**Actual dependency graph:**

```
                    ┌─────────────────────────┐
                    │       NeuronCore         │
                    │ (math, net, file, tasks, │
                    │  timers, events, debug,  │
                    │  serialization, WinRT)   │
                    └────────────┬─────────────┘
                 ┌───────────────┴───────────────┐
                 │                               │
        ┌────────▼───────┐              ┌────────▼────────┐
        │  NeuronClient  │              │  NeuronServer   │
        │ (D3D11 graphics│              │ (server engine  │
        │  + OpenglDirectx│             │  library stub)  │
        │  GL-compat,    │              └────────┬────────┘
        │  audio, input, │                       │
        │  GUI)          │                       │
        └────────┬───────┘                       │
                 │                               │
        ┌────────▼───────┐              ┌────────▼────────┐
        │   GameLogic    │              │     Server      │
        │ (Darwinia sim  │              │ (console server │
        │  + rendering)  │              │  executable)    │
        └────────┬───────┘              └─────────────────┘
                 │
        ┌────────▼───────┐
        │   EarthRise    │
        │ (Win32 client  │
        │  executable)   │
        └────────────────┘
```

> Note: `GameLogic` is linked by the **client** (EarthRise), not by the server. There is
> currently no server-authoritative simulation split — the server target is a stub.

## Key Architecture Decisions

- **Restructured Darwinia**: Original Darwinia game/engine code split into `Neuron*` engine
  libraries (`NeuronCore`/`NeuronClient`/`NeuronServer`), `GameLogic` (the game), and two
  executables (`EarthRise`, `Server`).
- **Direct3D 11 rendering**: `Neuron::Graphics::Core` owns the D3D11 device, swap chain, and
  views (`ID3D11Device`, `ID3D11DeviceContext`, render/depth views).
- **OpenGL compatibility layer**: `OpenglDirectx` emulates the original game's OpenGL calls on
  top of D3D11. This is **legacy**; prefer native D3D11 for new rendering work and do not
  extend the GL-emulation layer.
- **DirectXMath migration**: New and migrated math uses SIMD types via `Neuron::Math`
  (`NeuronCore/GameMath.h`). Legacy `LegacyVector2/3`, `Matrix33/34`, and `MathCommon` are the
  types being migrated away from.
- **C++/WinRT**: `NeuronCore.h` includes WinRT projections and `using namespace winrt`, so
  both client and server pull in WinRT. COM smart pointers use `winrt::com_ptr`.

## Documentation

| Document | Purpose |
|---|---|
| [coding-standards.md](coding-standards.md) | Naming, formatting, language conventions, native-first rule |
| [copilot-instructions.md](copilot-instructions.md) | Code-generation guidance for this repository |

## Setup Commands

- Configure (Debug x64): `cmake --preset x64-debug`
- Build (Debug x64): `cmake --build --preset x64-debug` *(if a build preset is defined; otherwise `cmake --build out/build/x64-debug`)*
- Configure (Release x64): `cmake --preset x64-release`
- Configure (Debug x86): `cmake --preset x86-debug`
- Configure (Release x86): `cmake --preset x86-release`

Presets use the **Ninja** generator with `cl.exe` and a custom SegmentHeap top-level include.
There is no vcpkg manifest — C++/WinRT and DirectX come from the Windows SDK.

## Development Workflow

- Configure and build via CMake presets (see Setup Commands above).
- Build output goes to a single directory: `${CMAKE_BINARY_DIR}/bin`.
- MSVC Edit-and-Continue debug info is enabled (CMP0141) for Debug/RelWithDebInfo.
- No hot reload/watch mode is configured. No required environment variables are defined.

### Detected Versions

| Component | Value |
|---|---|
| Build System | CMake 3.21+ with the Ninja generator and `cl.exe` |
| C++ Standard | C++23 (`CMAKE_CXX_STANDARD 23`, extensions off) — uses `<mdspan>`, `<format>`, `<ranges>`, WinRT coroutines |
| C++/WinRT | From the Windows SDK (`winrt/Windows.*` headers) — **not** vcpkg |
| Graphics API | Direct3D 11 (with a legacy OpenGL-over-D3D compatibility layer) |
| Platforms | x64 and x86 (Debug + Release presets for both) |
| Dependency manager | None (no `vcpkg.json`); Win32/DirectX/Winsock libs auto-linked via `#pragma comment(lib, …)` |

## Testing Instructions

- There is currently **no test project or test framework** in the repository.
- Manual validation (build + run the client) is expected after rendering or gameplay changes.
- If unit tests are added later, choose a framework explicitly with the maintainer — do not
  invent one unprompted.

## Code Style

- Naming: files `PascalCase.cpp/.h`, classes/structs `PascalCase`, functions `PascalCase` or
  `camelCase` (match the file), members `m_` + `camelCase`, globals `g_` prefix, constants
  `UPPER_SNAKE_CASE` (some legacy `constexpr` use a `c_` prefix — match the surrounding file).
- Indentation: 2 spaces in `Neuron*` engine code; legacy Darwinia `GameLogic` may use tabs.
  Match the file you are editing.
- Include guards: `#pragma once`.
- Assertions/logging: `ASSERT`, `ASSERT_TEXT`, `DEBUG_ASSERT`, `DEBUG_WARNING`,
  `Neuron::DebugTrace`, `Neuron::Fatal` (see `NeuronCore/Debug.h`).
- PCH: every project uses `pch.h` / `pch.cpp`. New `.cpp` files must include `pch.h` first.
  Most `pch.h` files simply include the project's umbrella header (`NeuronCore.h` /
  `NeuronClient.h`).
- COM pointers: use `winrt::com_ptr<T>`, `.get()`, and `= nullptr` (not
  `Microsoft::WRL::ComPtr`, `.Get()`, `.Reset()`). Use `IID_GRAPHICS_PPV_ARGS` (in
  `DirectXHelper.h`) when the target is a `com_ptr`.
- **Native-first**: do not write wrapper functions/classes that merely forward to an existing
  API — call the native API directly. See [coding-standards.md](coding-standards.md).
- Full standards in [coding-standards.md](coding-standards.md).

## Native-First — No Wrapper Functions or Classes

When developing, comply with [coding-standards.md](coding-standards.md). A core rule:
**do not develop wrapper functions or wrapper classes.** Keep development as native as possible.

- Use the native API directly rather than a thin pass-through wrapper that only renames or
  forwards calls.
- Prefer **DirectXMath** (`XMVECTOR`/`XMMATRIX`, `Neuron::Math`) directly over the legacy
  `LegacyVector2/3` / `Matrix33/34` wrapper types.
- Prefer native **Direct3D 11** calls over the `OpenglDirectx` GL-emulation layer; do not
  extend that compatibility layer.
- Prefer `winrt::com_ptr` and native COM/WinRT projections directly — do not re-wrap them.
- Prefer standard-library containers/algorithms/smart pointers directly over custom
  equivalents (`Darray`, `Llist`, `FastDarray`, …) in new code.
- A new class or function is justified only when it adds real behavior or invariants — not
  when it merely forwards to something that already exists.

## DirectXMath / Vector Conventions

New and migrated math uses `Neuron::Math` (`NeuronCore/GameMath.h`), which is built on
DirectXMath. All vector and matrix math must use SIMD register types (`XMVECTOR`, `XMMATRIX`)
for computation. `XMFLOAT3` / `XMFLOAT4X4` are **storage only** — never perform arithmetic on
them. The load→compute→store boundary must be explicit.

| Context | Type | Reason |
|---|---|---|
| Struct/class members | `XMFLOAT3` / `XMFLOAT4X4` | Stable layout, serializable, no alignment requirement |
| Function parameters (non-virtual) | `FXMVECTOR` + `XM_CALLCONV` | SIMD register passing |
| Function parameters (virtual) | `const XMFLOAT3&` | Virtual dispatch cannot use `XM_CALLCONV` |
| Function return values | `XMVECTOR` or `XMFLOAT3` | `XMVECTOR` if caller keeps computing; `XMFLOAT3` if storing |
| Local temporaries in functions | `XMVECTOR` / `XMMATRIX` | **Always** — never `XMFLOAT3` for intermediates |
| Loop bodies | `XMVECTOR` / `XMMATRIX` | Hoist loads before loop, store after |
| One-shot scalar queries | `XMFLOAT3` overload (e.g. `Length(XMFLOAT3)`) | Returns scalar — no vector kept in register |

> **Anti-pattern — do NOT add `XMFLOAT3` arithmetic operators** (`operator+`, `operator*`,
> etc.). They hide a load→op→store per expression, defeating the SIMD boundary. All vector
> arithmetic stays in `XMVECTOR`.

```cpp
// Correct — vector stays in registers
[[nodiscard]] float XM_CALLCONV Distance(FXMVECTOR _a, FXMVECTOR _b);

// Incorrect — forces store-to-memory then reload
[[nodiscard]] float Distance(const XMFLOAT3& _a, const XMFLOAT3& _b);
```

- All new math utility functions go in `Neuron::Math` (`NeuronCore/GameMath.h`) — do not add
  math methods to storage types, and (per native-first) do not extend the legacy
  `LegacyVector*` / `Matrix3x` wrapper types in new code.
- Follow the DirectXMath `FXMVECTOR` / `GXMVECTOR` / `HXMVECTOR` / `CXMVECTOR` parameter-order
  rules (see the [DirectXMath calling conventions](https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-internals#calling-conventions)).

## Build and Deployment

- Build via CMake presets. Use Debug for local dev and Release for shipping builds.
- The `EarthRise` target copies `GameData/` to an `Assets/` folder next to the executable as a
  post-build step (`FileSys::SetHomeDirectory()` roots asset paths there at runtime).
- Confirm the targets you touched build cleanly before merging.

## Pull Request Guidelines

- Title format: `[component] Brief description`.
- Build the affected configurations (at least x64 Debug and Release) before submitting.
- Include a short summary of gameplay/graphics impact when applicable.

## Additional Notes

- Avoid adding includes already covered by `pch.h`.
- `Neuron*` engine code favors modern C++20/23; legacy Darwinia `GameLogic` uses older patterns
  (raw pointers, C-style strings, custom containers). Keep those when editing legacy areas;
  use modern C++ for new code.
- Build after changes to confirm compilation succeeds.
</content>
</invoke>
