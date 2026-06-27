# AGENTS.md

## Project Overview

Deepspace Outpost is a Windows-native C++ game built with CMake and the MSVC toolchain. It
targets Windows (x64 and x86) and builds with the Ninja generator via CMake presets. The
renderer is **Direct3D 11** and audio is **XAudio2**. The codebase is mid-restructure toward a
modular engine split into reusable `Neuron*` static libraries plus the game logic and the
executables (a Win32 GUI client, a headless bot test client, and a dedicated server). Vector/matrix
math is being migrated to **DirectXMath** (SIMD) via `Neuron::Math`.

**Direction:** the project is migrating from the single-player game to an **open-world,
server-authoritative MMO** (first milestone: up to 100 players) on an `int64³` coordinate field,
with an **in-house ECS**, client prediction, and Area-of-Interest replication. The detailed phased
plan lives in [`docs/MIGRATION_ROADMAP.md`](docs/MIGRATION_ROADMAP.md) — read it before doing
architecture work.

> **Status note.** Today the whole game builds as a single Win32 GUI executable,
> **DeepspaceOutpost**, with two source tiers: faithfully ported game logic (`*.cpp` in the
> project root, compiled `/permissive`) and a freshly written platform layer (`platform/*.cpp` —
> Win32 / Direct3D 11 / XAudio2, compiled `/permissive- /W4`) wired to the game through contract
> headers (`gfx.h`, `sound.h`, …). The `Neuron*` and `Server` directories are placeholders for the
> *planned* engine libraries described below and are empty for now. `DemoShaders/` is HLSL ported
> from the engine's GLSL, kept for **reference only** (not built). Several engineering conventions
> below (DirectXMath SIMD boundary, `winrt::com_ptr`, native-first) describe the *target* style for
> new and migrated code; the ported game logic still uses legacy patterns. Match the file you are
> editing, and move legacy code toward the target style when you touch it.

**Target project structure (CMake), once the engine split lands:**

| Project | Type | Role |
|---|---|---|
| **NeuronCore** | Static lib | Engine foundation **+ the only client/server *shared data***: the **in-house ECS** container, the component & wire-protocol **schemas** (`Transform`, `Motion`, `ShipDef`, …), static ship-data tables, math (`GameMath`/`Neuron::Math` over DirectXMath, plus legacy `LegacyVector2/3`, `Matrix33/34`, `MathCommon`), networking (`NetLib` — raw-winsock **UDP** sockets, custom reliability layer, packets, connections), filesystem (`FileSys`), tasks/threads (`TasksCore`), timers, events, debug, serialization (`DataReader`/`DataWriter` hand-rolled binary; `Json` cold path). **No game behavior.** C++/WinRT via `NeuronCore.h`. |
| **NeuronClient** | Static lib | Client engine: Direct3D 11 graphics core (`GraphicsCore`, `TextureManager`), the `OpenglDirectx` GL-over-D3D compat layer (legacy), audio, input, GUI/window manager, **plus client networking** (session, **snapshot interpolation + dead-reckoning** — presentation only, *no game rules*). Depends on NeuronCore. |
| **NeuronServer** | Static lib | Server engine: authoritative session management, **AOI/replication**, and **persistence (Microsoft SQL Server)**. Depends on GameLogic, NeuronCore. |
| **GameLogic** | Static lib | **SERVER-ONLY — the single home of ALL game behavior**: motion/physics integration, AI/tactics, economy/market, combat resolution, missions, spawning/encounters. Headless, no rendering. Depends on **NeuronCore**. **The client never links it; there is no shared game-logic library.** |
| **DeepspaceOutpost** | Win32 GUI executable | Game client: main loop, input, game-specific rendering (wireframe/HUD via the render queue), UI, audio. Entry point `wWinMain`. Links NeuronClient. |
| **BotClient** | Console executable | **Headless test client** — scripted/AI bots, **no render/audio**, driving the real net stack for load/soak testing (incl. the 100-player test). Links NeuronClient (headless, no graphics init). |
| **Server** | Console executable | Dedicated-server host: main loop, sessions, fixed-tick scheduler. Entry point `main`. Links NeuronServer, GameLogic. |

**Target dependency graph** (each project depends on its parent; arrows omitted for clarity):

```
NeuronCore                 engine + SHARED DATA ONLY: ECS container, component/protocol schemas, ship-data, math, NetLib
├─ NeuronClient            D3D11 · audio · input · GUI · client net (interpolation + dead-reckoning, NO game rules)
│  ├─ DeepspaceOutpost     Win32 client exe (game rendering, UI, input)
│  └─ BotClient            headless test exe (bots, no render/audio)
└─ GameLogic               SERVER-ONLY: ALL game behaviour (motion/physics, AI, economy, combat, missions)
   └─ NeuronServer         sessions · AOI/replication · MS SQL persistence
      └─ Server            dedicated-server host exe
```

> **Note (server-authoritative MMO target).** `GameLogic` is the **only game-logic library and
> is server-only** — there is **no shared game-logic library**. The client is a **thin
> presentation layer** that runs no game rules: it forwards input and renders interpolated +
> dead-reckoned authoritative snapshots. Client and server share only **data** (the ECS/protocol
> schemas in `NeuronCore`), never behavior. This is a deliberate change from the older
> client-linked-`GameLogic` design. Today the game still builds as the single-player client (see
> the status note above); the phased path is in [`docs/MIGRATION_ROADMAP.md`](docs/MIGRATION_ROADMAP.md).

## Key Architecture Decisions

- **Server-authoritative MMO (in migration)**: target is a **massive seamless** open-world,
  server-authoritative MMO (up to 100 players) on one continuous **`int64³`** coordinate space
  (no visible boundaries; an invisible cell partition drives interest and future sharding) with
  **multi-resolution Area-of-Interest** replication. **All game behavior is server-only** in
  `GameLogic`; the client runs **no game logic** — it is a thin presentation layer (snapshot
  interpolation + dead-reckoning). **No shared game-logic library**: client and server share only
  *data* (the in-house **ECS** container + component/protocol schemas in `NeuronCore`). The
  de-globalized `Universe` *is* the ECS world, simulated by the server. Transport is **raw-winsock
  UDP** + a custom reliability layer; the wire format is a **hand-rolled binary** protocol
  (`DataReader`/`DataWriter`). See [`docs/MIGRATION_ROADMAP.md`](docs/MIGRATION_ROADMAP.md).
- **Built for a 4X / RTS drift**: the game logic will move over time from single-ship space-flight
  toward a **4X / RTS** style (many units per player, empire/economy/territory, less twitch). The
  architecture generalizes three single-player assumptions up front so that pivot is an extension,
  not a rewrite: **player ≠ avatar** (identity is Account → Empire/Faction → owns N entities;
  camera/interest are view-driven), **command/intent input** (validated orders, reusing the
  existing `flight_roll/climb/speed/fire` intent state), and **decoupled sim/command/replication
  clocks**. The ECS is indexed **relationally** (owner/faction/group/tag) as well as spatially.
  See §2.4 of the roadmap.
- **Modular engine split**: Game/engine code organized into `Neuron*` engine
  libraries (`NeuronCore`/`NeuronClient`/`NeuronServer`), `GameLogic` (the **server-only** home
  of all game behavior), and the executables (`DeepspaceOutpost`, `BotClient`, `Server`).
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
| [docs/MIGRATION_ROADMAP.md](docs/MIGRATION_ROADMAP.md) | **Phased plan** to migrate the single-player client to the server-authoritative MMO (read first for architecture work) |
| [coding-standards.md](.github/coding-standards.md) | Naming, formatting, language conventions, native-first rule |
| [copilot-instructions.md](.github/copilot-instructions.md) | Code-generation guidance for this repository |
| [DemoShaders/PORTING.md](DemoShaders/PORTING.md) | GLSL→HLSL porting guide for the reference shaders in `DemoShaders/` (reference only — not built) |

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

- Naming: files `PascalCase.cpp/.h`, classes/structs `PascalCase`, **functions/methods
  `PascalCase`** (per `coding-standards.md`), local variables `camelCase`, members `m_` +
  `camelCase`, globals `g_` prefix, constants `UPPER_SNAKE_CASE` (some legacy `constexpr` use a
  `c_` prefix — match the surrounding file). When editing a still-legacy ported file that is all
  `snake_case`, match that file until it is modernized.
- Indentation: 2 spaces in `Neuron*` engine code; the legacy ported game logic may use tabs.
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
  API — call the native API directly. See [coding-standards.md](.github/coding-standards.md).
- Full standards in [coding-standards.md](.github/coding-standards.md).

## Native-First — No Wrapper Functions or Classes

When developing, comply with [coding-standards.md](.github/coding-standards.md). A core rule:
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
- The `DeepspaceOutpost` target copies `GameData/` next to the executable as a post-build step,
  so the runtime reads assets (textures, sounds, fonts, `*.cfg`) from the executable's directory.
- Confirm the targets you touched build cleanly before merging.

## Pull Request Guidelines

- Title format: `[component] Brief description`.
- Build the affected configurations (at least x64 Debug and Release) before submitting.
- Include a short summary of gameplay/graphics impact when applicable.

## Additional Notes

- Avoid adding includes already covered by `pch.h`.
- `Neuron*` engine code favors modern C++20/23; the legacy ported game logic uses older patterns
  (raw pointers, C-style strings, custom containers). Keep those when editing legacy areas;
  use modern C++ for new code.
- Build after changes to confirm compilation succeeds.
</content>
</invoke>
