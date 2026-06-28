# GitHub Copilot Instructions

## Priority Guidelines

When generating code for this repository:

1. **Version Compatibility**: Detect and respect the exact versions of languages, frameworks, and libraries in use (C++23, Windows SDK C++/WinRT, Direct3D 11).
2. **Native-First**: Use native APIs directly. **Never generate wrapper functions or wrapper classes** that merely forward to, rename, or thinly adapt an existing API. See `.github/coding-standards.md`.
3. **Codebase Patterns**: When context files don't provide specific guidance, scan the codebase for established patterns and follow the most consistent one.
4. **Architectural Consistency**: Maintain the existing multi-project modular architecture and dependency boundaries.
5. **Code Quality**: Prioritize performance and correctness.

## Project Snapshot

**Deepspace Outpost** is a Windows-native C++23 game (DirectX 11 / XAudio2). The codebase is
mid-restructure toward a modular engine: reusable `Neuron*` static libraries plus the game logic
and two executables.

> **Current status.** Today the whole game builds as a single Win32 GUI executable,
> **DeepspaceOutpost**, made of two source tiers: faithfully ported game logic (`*.cpp` in the
> project root, compiled `/permissive`) and a freshly written platform layer (`platform/*.cpp` —
> Win32 / Direct3D 11 / XAudio2, compiled `/permissive- /W4`) that talks to the game through
> contract headers (`gfx.h`, `sound.h`, …). The `Neuron*` and `Server` directories are
> placeholders for the *planned* engine libraries below and are empty for now. `DemoShaders/`
> holds HLSL ported from the engine's GLSL and is **reference only** — it is not part of the build.

**Direction:** the project is migrating to an **open-world, server-authoritative MMO** (up to 100
players, `int64³` world, in-house ECS, client prediction, AOI replication). Read
[`docs/MIGRATION_ROADMAP.md`](../docs/MIGRATION_ROADMAP.md) before architecture work.

Target structure once the engine split lands:

- **NeuronCore** (static lib): engine foundation **+ the only client/server shared data** — the
  **in-house ECS** container, component & wire-protocol **schemas** (`Transform`, `Motion`,
  `ShipDef`, …), static ship-data tables, math, networking (`NetLib` — raw-winsock UDP +
  reliability), filesystem, tasks, timers, events, debug, serialization (`DataReader`/`DataWriter`
  hand-rolled binary; `Json` cold path). **No game behavior.** Includes C++/WinRT projections.
- **NeuronClient** (static lib): Direct3D 11 graphics core, the `OpenglDirectx` GL-over-D3D
  compatibility layer (legacy), audio, input, GUI, **plus client networking** (session, **snapshot
  interpolation + dead-reckoning** — presentation only, *no game rules*). Depends on NeuronCore.
- **NeuronServer** (static lib): server engine — sessions, AOI/replication, persistence (MS SQL).
  Depends on GameLogic.
- **GameLogic** (static lib): **SERVER-ONLY — the single home of ALL game behavior**: motion/
  physics, AI/tactics, economy, combat resolution, missions, spawning. Headless, no rendering.
  Depends on NeuronCore. **The client never links it; there is no shared game-logic library.**
- **DeepspaceOutpost** (Win32 GUI executable, `wWinMain`): the game client. Links NeuronClient.
- **BotClient** (console executable): **headless test client** — bots, no render/audio, for load
  testing. Links NeuronClient (headless).
- **Server** (console executable, `main`): dedicated-server host. Links NeuronServer, GameLogic.

The renderer is **Direct3D 11** (not D3D12). Math is migrating from the legacy ported math types to
**DirectXMath** via `Neuron::Math`. Several conventions below describe the *target* style for
new/migrated code; legacy ported code still uses older patterns — match the file you edit.

## Technology Version Detection

Before generating code, scan the codebase to identify:

1. **Language Versions**: `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 23` (extensions off).
   `CMakePresets.json` defines x64/x86 Debug/Release Ninja presets using `cl.exe`. Never use
   features beyond C++23.
2. **Framework Versions**: There is **no vcpkg manifest**. C++/WinRT and DirectX come from the
   Windows SDK. Do not assume a package manager or suggest packages that are not present.
3. **Library Versions**: Win32, DirectX, and Winsock libraries are auto-linked via
   `#pragma comment(lib, …)`. Generate code compatible with the headers actually included.

## Context Files

Prioritize the following files (if they exist):

- **AGENTS.md** (repo root): Project structure, dependency graph, and conventions — read first.
- **docs/MIGRATION_ROADMAP.md**: Phased plan for the server-authoritative MMO migration.
- **.github/coding-standards.md**: Code style, formatting standards, and the native-first rule.

If a `.github/copilot/` directory is added later (architecture.md, tech-stack.md, etc.),
prioritize it. Otherwise use `.github/coding-standards.md` and the existing codebase.

## Codebase Scanning Instructions

When context files don't provide specific guidance:

1. Identify similar files to the one being modified or created.
2. Analyze patterns for: naming, code organization, error handling, logging, documentation, and any existing tests.
3. Follow the most consistent patterns found in the codebase.
4. When conflicting patterns exist, prefer the patterns in newer `Neuron*` engine / `platform/` code over the legacy ported game-logic patterns for *new* code, but match the existing file when *editing* it.
5. Never introduce patterns not found in the existing codebase.

## Code Quality Standards

- Follow existing patterns for memory and resource management.
- **Never wrap a native API.** Call DirectXMath, Direct3D 11, COM/WinRT, and the standard
  library directly. Add a new class/function only when it carries genuine behavior or
  invariants — not when it just forwards to something that already exists.
- Use `winrt::com_ptr` instead of `Microsoft::WRL::ComPtr` for COM smart pointers:
  - Use `.get()` instead of `.Get()` to obtain the raw pointer.
  - Use `= nullptr` instead of `.Reset()` to release a COM object.
  - Use `IID_GRAPHICS_PPV_ARGS(ptr)` (in `DirectXHelper.h`) instead of `IID_PPV_ARGS(&ptr)` when the target is a `com_ptr`. `IID_PPV_ARGS` is only correct with raw pointers (`T**`) or `.put()` / `.Put()`.
- **DirectXMath vector passing**: Pass 3D vectors as `FXMVECTOR` (not `const XMFLOAT3&`) with
  `XM_CALLCONV` to keep values in SIMD registers. Use `XMFLOAT3`/`XMFLOAT4X4` only for storage
  (struct members, serialization). Follow DirectXMath parameter-position rules
  (`FXMVECTOR`/`GXMVECTOR`/`HXMVECTOR`/`CXMVECTOR`). All new math functions go in `Neuron::Math`
  (`NeuronCore/GameMath.h`). Do not extend the legacy `LegacyVector2/3` / `Matrix33/34` types.

## Documentation Requirements

- Follow the documentation format found in the codebase.
- Match the comment style and completeness of existing comments.
- Comment the **why**, not the **what**.

## Testing Approach

- **GoogleTest is the standard unit-test framework.** Tests live in per-library projects under
  `Tests/<Library>/` (`Tests/NeuronCore`, `Tests/GameLogic`, `Tests/NeuronClient`,
  `Tests/NeuronServer`), written with `TEST(Suite, Name)` and `EXPECT_*` / `ASSERT_*`.
- GoogleTest is fetched via CMake `FetchContent` for the test binaries only — the one
  sanctioned exception to the native-first / no-third-party rule. The old hand-rolled
  `TestFramework.h` harness has been removed; do not reintroduce it.
- Tests register with CTest (`gtest_discover_tests`); run `ctest --output-on-failure` from the
  build directory. Manual validation (build + run the client) still applies where unit tests
  cannot cover behavior. See `AGENTS.md` → Testing Instructions for details.

## General Best Practices

- Follow naming conventions exactly as they appear in existing code.
- Match code organization, error handling, and logging patterns from similar files.
- Use the same approach to configuration as seen in the codebase.

## Project-Specific Guidance

- Respect the solution boundaries and dependency graph:
  - `DeepspaceOutpost` / `BotClient` (client exes) → NeuronClient → NeuronCore.
  - `Server` (server exe) → NeuronServer → GameLogic → NeuronCore.
  - `GameLogic` is the **server-only** home of ALL game behavior; **there is no shared game-logic
    library**. The client runs no game rules — it shares only data (ECS/protocol schemas in
    `NeuronCore`) and renders interpolated + dead-reckoned snapshots. (Change from the older
    client-linked-`GameLogic` design.)
- Use `ASSERT` / `ASSERT_TEXT` / `DEBUG_ASSERT` / `DEBUG_WARNING` for assertions.
- Use `Neuron::DebugTrace` for debug logging and `Neuron::Fatal` for fatal errors (see `NeuronCore/Debug.h`).
- Files are `PascalCase.cpp` / `PascalCase.h`; classes use `PascalCase`, **functions/methods
  `PascalCase`** (per `.github/coding-standards.md`), local variables `camelCase`, members `m_` +
  `camelCase`, globals `g_`, constants `UPPER_SNAKE_CASE` (some legacy `constexpr` use a `c_`
  prefix — match the file). Still-legacy `snake_case` ported files stay `snake_case` until modernized.
- Do not add `#include` directives already covered by `pch.h`. All projects use `pch.h` / `pch.cpp`,
  and most `pch.h` files just include the project umbrella header (`NeuronCore.h` / `NeuronClient.h`).
- Legacy ported game-logic code uses raw pointers, C-style strings, and custom containers
  (`Darray`, `Llist`, …); keep those patterns when editing those areas, but use modern C++ and
  standard-library types for new code.
- `Neuron*` engine code favors modern C++ (`std::string_view`, `std::format`, `constexpr`, `[[nodiscard]]`, `noexcept`).
- Rendering: the `OpenglDirectx` layer emulates OpenGL on Direct3D 11 and is **legacy** — do not
  extend it. Prefer native D3D11 (`Neuron::Graphics::Core`) for new rendering work.
- Build and verify after changes using CMake (`cmake --build` on the relevant preset/output dir).

## Data Serialization

- **Network packets**: Use `DataReader` / `DataWriter` (`NeuronCore`) for the network protocol.
- **Streamed I/O**: Use `BinaryStreamReaders` / `TextStreamReaders` for structured file reading.
- Match the serialization helpers already used by the surrounding code; do not introduce a new
  serialization layer that duplicates an existing one.

## Rendering Conventions

- **Graphics API**: Direct3D 11 via `Neuron::Graphics::Core` (D3D11 device, context, swap chain,
  render/depth views).
- **Legacy GL layer**: `OpenglDirectx` translates the original game's OpenGL calls to D3D11. It is
  a compatibility shim, not a foundation for new work — do not extend it.
- **Textures**: `TextureManager` / `DDSTextureLoader` handle texture loading.

## Performance Constraints

- **`EventManager`**: synchronizes on every call — reserve it for infrequent events. Use direct
  calls for hot-path game updates.
- **`ASyncLoader`**: be mindful of blocking/spin-wait behavior; do not block the main thread on it.

## Native-First (Most Important Rule)

Keep development as native as possible. **Do not create wrapper functions or wrapper classes.**
If an API already exists (DirectXMath, Direct3D 11, COM/WinRT, the C++ standard library), call it
directly. A wrapper that only forwards, renames, or thinly adapts an existing call is not allowed.
Replace legacy wrappers (`LegacyVector*`, `Matrix3x`, `OpenglDirectx`, custom containers) with the
native equivalent when you touch them, rather than adding more wrappers on top.
</content>
