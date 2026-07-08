# DeepspaceOutpost — ServerManager Design

**Status:** design document, written 2026-07-08 against the as-built code
(`Server/GameServer.h`, `GameLogic/ServerSessions.h`, `NeuronServer/TickMetrics.h`,
`NeuronCore/Messages/*`, `NeuronClient/ClientEngine.h`, `NeuronClient/gui/*`
verified in source). Designs **ServerManager** — a standalone Windows GUI
application that connects to a running dedicated server by IP over a **dedicated
management channel** and shows a live status screen: who is connected, what is
happening (events), and the server's health. The channel and app are shaped so
that remote **settings management and admin commands** are a later extension of
the same protocol, not a redesign.

**Owner decisions recorded here (2026-07-08):**

| Question | Decision |
|---|---|
| How is the management channel separated from game traffic? | **A second UDP port** (default `40001` = game port + 1). Its own socket, its own `DatagramPump` drain with its own budget — game traffic and its receive budget are untouched, and the port can be firewalled/VPN-restricted independently of the public game port. |
| How does the ServerManager authenticate? | **Shared admin secret.** The server reads an admin key from the `DSO_ADMIN_KEY` environment variable (the same opt-in pattern as `DSO_DB`); when unset, the management channel is **entirely disabled** — no socket is opened. The manager presents the key in its `AdminHello`; the accept reply carries a CSPRNG **admin token** (from the existing `SecureRandom64`) stamped on every subsequent manager datagram — the same token-not-endpoint identity model as player sessions (B2). |
| v1 scope | **Read-only status.** Roster, event feed, health metrics. The message-id space and handshake are laid out with command messages (settings, kick, broadcast, save) in mind, but v1 implements none of them. |
| History on connect | **Recent ring buffer.** The server keeps a bounded in-memory ring of recent admin events (last 256); a newly accepted manager gets the ring replayed, then streams live. No persistence involved. |

Nothing here touches the anti-cheat or game-protocol boundary: the management
channel is a **separate socket** speaking the **same tested reliable framing**
(`MessageEndpoint`/`ReliableChannel`), the game's `NMSG`/`NRLB`/snapshot lanes are
unchanged, and the server's simulation reads nothing from the admin side. This is
a **NeuronCore schema + NeuronServer service + NeuronClient endpoint + one new
executable** design.

**Naming.** New identifiers follow
[`.github/coding-standards.md`](.github/coding-standards.md); existing symbols
are referenced by their real names.

---

## 0. The target model

1. **A separate management channel.** The dedicated server, when an admin key is
   configured, opens a second UDP socket on the management port. Everything an
   admin does rides this socket; nothing about the game port changes. The
   channel reuses the existing reliable-lane transport (`Msg::MessageEndpoint`
   over `Net::UdpSocket`) — ordering, acks, resends, MTU packing and lane
   isolation are already built and unit-tested.
2. **A thin observing client.** ServerManager is a small Win32 GUI executable
   (like `DeepspaceOutpost`, minus the game): `ClientEngine` window + D3D11
   device, `Canvas`/`GuiWindow` UI, and an `AdminClient` network endpoint (a
   sibling of `ReplicationClient`, an order of magnitude smaller). It holds a
   typed local mirror — roster map, event log, latest health sample — that the
   GUI renders every frame.
3. **The server pushes; the manager renders.** After the handshake the server
   replays the event ring and the current roster, then pushes deltas: a roster
   entry when a player joins/leaves/changes, an `AdminEvent` when something
   happens, an `AdminHealth` sample once a second. The manager sends nothing but
   its handshake and acks in v1 — a read-only wire by construction, not by
   convention.
4. **Commands are a future message range, not a future protocol.** Settings
   updates and admin actions (kick, broadcast, save, shutdown) get reserved
   message ids and a documented path (§8); the handshake, identity, and channel
   don't change when they land.

---

## 1. Current state evaluation — what already exists

### 1.1 Transport and framing (reused wholesale)

| Piece | Where | Reuse |
|---|---|---|
| Non-blocking UDP socket | `NeuronCore/NetLib.h` (`Net::UdpSocket`) | The admin socket is just a second instance, bound to the management port. |
| Bounded datagram drain + magic routing | `NeuronServer/DatagramPump.h` | The admin socket gets its own `PumpDatagrams` call with its own (small) budget; a flood on the admin port cannot eat game-tick time. |
| Reliable, ordered, acked messages over UDP | `NeuronCore/ReliableChannel.h` | Used as-is (inside `MessageEndpoint`). |
| Per-peer lanes (Control/Gameplay/Bulk) + token stamping | `NeuronCore/Messages/MessageEndpoint.h` (`NRLB` magic) | Used **verbatim** on the admin socket. The port separates the channels, so the same `RELIABLE_MAGIC` is fine; the token field carries the **admin token**. Lane mapping: Control = handshake, Gameplay = live events/health, Bulk = ring/roster replay. |
| Typed catalog messages (`Fields()` tuple codec, `REGISTER_MESSAGE`) | `NeuronCore/Messages/Registry.h`, `Serialize.h` | New admin messages are ordinary catalog messages — codec, registry and round-trip test patterns already exist. |
| CSPRNG tokens | `Server/SecureRandom.h` (`SecureRandom64`) | Admin token source. |

### 1.2 The data the status screen needs (all already on the server)

| Status-screen need | Source today |
|---|---|
| Who is connected | `GameLogic::ServerSessions` / `Session` (`GameLogic/ServerSessions.h:91`): `name`, `playerId`, `endpoint`, `entity`, `score`, `rttMs`, `lastSeenTick`, `loading` |
| Events happening | Existing `GameServer` hook points: hello accepted/resumed (join/reconnect), session reap (leave), `OnEntityKilled` (kills), `HandleChat` (chat), `OnCrime` (wanted changes), respawn logging, overrun notes |
| Server health | `Neuron::Server::TickMetrics` (`NeuronServer/TickMetrics.h`): per-window ticks, overruns, avg/max tick ms, entities, sessions, bytes/s, dropped entities — already summarized every `METRICS_WINDOW_TICKS` (~5 s) for the console line |

Today all of this dead-ends in `printf` console lines and the in-process
structs. The management channel is the second consumer.

### 1.3 The GUI stack (reused wholesale)

`ClientEngine::Startup` (`NeuronClient/ClientEngine.h`) creates the Win32 window,
the native D3D11 device (`Neuron::Graphics::Core`), `Render2D`, fonts/strings and
the GUI `Canvas`. `Canvas` (`NeuronClient/gui/Canvas.h`) manages registered
`GuiWindow`s (focus, z-order, mouse/keyboard routing, the single 2D pass);
`GuiWindow`/`GuiButton` provide movable titled windows with buttons and text
edit; custom content (lists, meters) is drawn in a window's `Render()` override
via `Render2D`/`TextRenderer` — exactly how the game's own windows
(`DeepspaceOutpost/GameWindows.cpp`) do it. `ClientEngine::Frame(frameCapMs)`
drives one whole frame including the OS message pump — ServerManager's main loop
is a `for (;;) ClientEngine::Frame(...)` with an app `Update()` in a `GameMain`
subclass, the same lifecycle the game uses.

### 1.4 What does NOT exist yet

- No admin/management anything: no second socket, no admin identity, no event
  ring, no remote metrics consumer (`docs/ARCHITECTURE.md` has no management
  channel; this document is its design).
- No runtime-mutable server settings: `Server/ServerConfig.h` knobs are
  `constexpr`. (This is why settings editing is future work — §8.2.)
- No non-game GUI executable precedent — ServerManager is the first, and proves
  the engine split (`NeuronClient` usable without the game).

---

## 2. Architecture overview

```
                       game clients (unchanged)
                            │  UDP :40000  (NMSG / NRLB / snapshot magics)
                            ▼
   ┌────────────────────────────────────────────────────────────┐
   │ Server.exe                                                 │
   │  GameServer::RunTick()                                     │
   │   ReceiveDatagrams ─ game socket pump (unchanged)          │
   │   PumpAdmin ──────── admin socket pump  ◄── NEW            │
   │   ... simulation phases (unchanged) ...                    │
   │   PublishState                                             │
   │   PublishAdmin ───── roster deltas · events · health ◄ NEW │
   │                                                            │
   │  Neuron::Server::AdminChannel   (NeuronServer, headless)   │
   │   · admin session table (token-keyed, like ServerSessions) │
   │   · event ring buffer (256, replayed on connect)           │
   │   · per-admin MessageEndpoint (Control/Gameplay/Bulk)      │
   └────────────────────────────────────────────────────────────┘
                            ▲  UDP :40001  (NRLB reliable lanes only)
                            │  AdminHello(key) ⇄ AdminHelloAck(token)
                            │  ── then server-push: roster / events / health
   ┌────────────────────────────────────────────────────────────┐
   │ ServerManager.exe  (Win32 GUI, links NeuronClient)         │
   │  ManagerApp : Neuron::GameMain                             │
   │   · AdminClient (NeuronClient) — socket + MessageEndpoint, │
   │     typed mirror: roster map · event log · health history  │
   │   · GuiWindows on Canvas: Connect · Players · Events ·     │
   │     Health · status bar                                    │
   └────────────────────────────────────────────────────────────┘
```

Dependency placement follows the existing split exactly:

| New piece | Library | Why there |
|---|---|---|
| `Messages/Defs/Admin.h` — the wire schema | **NeuronCore** | Shared data only, like every other message def. Both ends include it. |
| `AdminChannel` — server-side service | **NeuronServer** | Sibling of `DatagramPump`/`TickMetrics`/`OnChangeCache`: generic server plumbing, headless and unit-testable. Takes **plain data** (strings/ints) from `GameServer`, not `GameLogic` types, so it stays pure. |
| `AdminClient` — manager-side endpoint | **NeuronClient** | Sibling of `ReplicationClient`: client networking, socket-owning, headless-testable. |
| `ServerManager/` — the GUI exe | **new top-level target** | Like `DeepspaceOutpost`: entry point + app-specific windows only. Links NeuronClient. |
| Admin socket + wiring + event taps | **Server** exe (`GameServer`/`Main.cpp`) | The host owns process-lifetime sockets and knows the hook points. |

---

## 3. The management wire protocol

### 3.1 Channel and framing

- **Port:** `Cfg::ADMIN_PORT = 40001` in `ServerConfig.h`; overridable by a
  server command-line argument (the game port already is, `Server/Main.cpp:22`).
- **Enable gate:** the socket is opened **only** when `DSO_ADMIN_KEY` is set —
  the whole feature is off otherwise, byte-for-byte identical server behavior
  (the `DSO_DB` pattern, `GameServer.cpp` `MakePersistenceService`).
- **Framing:** every datagram on this port is an `NRLB` reliable-lane datagram
  (`MessageEndpoint::WriteDatagrams` / `OnDatagram`). There is **no unreliable
  lane** on the management channel — everything an admin sees must arrive, and
  the volume is trivial (bytes per second, not kilobytes per tick).
- **Identity:** the token field of the `NRLB` header carries `0` on the opening
  `AdminHello` and the **admin token** afterwards. The server authenticates by
  token before routing (reusing `Msg::PeekReliableToken`), so a spoofed source
  address without the token is dropped before any decode, and an admin NAT
  rebind heals silently — the same B2 model as players.

### 3.2 Message ids

Admin messages live in the already-reserved **debug/tooling wire range
`0x0F00–0x0FFF`** (`NeuronCore/Messages/MessageId.h`) — wire-visible
diagnostics is exactly what this is. Ids are permanent ABI once shipped (§4.3
discipline in ARCHITECTURE.md).

| Id | Message | Dir | Lane | Purpose |
|---|---|---|---|---|
| `0x0F00` | `AdminHello` | M→S | Control | Handshake: protocol version + admin key |
| `0x0F01` | `AdminHelloAck` | S→M | Control | Accept: admin token + server identity block |
| `0x0F02` | `AdminHelloReject` | S→M | Control | Refuse: bad key / version mismatch |
| `0x0F03` | `AdminPlayerInfo` | S→M | Gameplay (Bulk on replay) | One roster entry, sent on join/change and replayed on connect |
| `0x0F04` | `AdminPlayerGone` | S→M | Gameplay | A player left (reaped/disconnected) |
| `0x0F05` | `AdminEvent` | S→M | Gameplay (Bulk on replay) | One event-feed line (typed kind + text) |
| `0x0F06` | `AdminHealth` | S→M | Gameplay | Periodic health sample (~1 Hz) |
| `0x0F10+` | *(reserved)* | M→S | Control | Future commands: settings, kick, broadcast, save, shutdown (§8) |

### 3.3 Message layouts (catalog messages, `Fields()` codec)

```cpp
// NeuronCore/Messages/Defs/Admin.h  (all REGISTER_MESSAGE'd)

// manager -> server: the opening handshake. Token field of the datagram is 0.
struct AdminHello
{
  uint32_t    protocolVersion = 0;   // Msg::PROTOCOL_VERSION (one shared version)
  std::string adminKey;              // the DSO_ADMIN_KEY secret, length-capped
};

enum class AdminRejectReason : uint8_t { ProtocolMismatch = 1, BadKey = 2 };

// server -> manager: accepted. Carries the identity block the header bar shows.
struct AdminHelloAck
{
  uint64_t adminToken = 0;        // CSPRNG (SecureRandom64); stamp all datagrams
  uint16_t protocolVersion = 0;   // echo
  uint32_t gameLogicVersion = 0;  // GameLogic::Version()
  uint32_t tickRateMs = 0;        // Cfg::TICK_SLEEP_MS
  uint32_t serverTick = 0;        // world tick at accept (uptime baseline)
  uint16_t gamePort = 0;          // the game port this server serves
};

struct AdminHelloReject { uint8_t reason = 0; };

// server -> manager: one roster row. Sent on join / name change / rename /
// score change (change-gated), and replayed for every live session on connect.
struct AdminPlayerInfo
{
  uint32_t    playerId = 0;     // roster key (stable across reconnects)
  uint32_t    entityId = 0;     // primary hull
  std::string name;
  uint32_t    address = 0;      // client IPv4 (host order) — admin-visible
  uint16_t    port = 0;
  uint32_t    rttMs = 0;        // session's reported RTT (E1)
  int32_t     score = 0;
  uint8_t     state = 0;        // 0 live, 1 loading, 2 pending-shell
  uint32_t    connectedTick = 0;
};

struct AdminPlayerGone { uint32_t playerId = 0; uint8_t reason = 0; };  // 0 quit/timeout, 1 kicked (future)

// server -> manager: one event-feed line. `sequence` is the ring's monotonic
// counter, so the manager can both order the feed and detect the replay/live seam.
enum class AdminEventKind : uint8_t
{
  ServerStarted = 1, PlayerJoined, PlayerLeft, PlayerReconnected,
  Kill, Chat, Crime, TickOverrun,
};
struct AdminEvent
{
  uint32_t    sequence = 0;
  uint32_t    tick = 0;         // world tick when it happened
  uint8_t     kind = 0;         // AdminEventKind
  uint32_t    subject = 0;      // playerId or entity index (kind-dependent; 0 = none)
  std::string text;             // preformatted display line ("Cmdr JAMESON docked...")
};

// server -> manager: rolling health, one sample per metrics window (~1 s cadence
// for admins; the console's 5 s line is unchanged). Mirrors TickSummary.
struct AdminHealth
{
  uint32_t tick = 0;
  uint32_t uptimeSeconds = 0;
  float    avgTickMs = 0.f;
  float    maxTickMs = 0.f;
  uint32_t overruns = 0;        // cumulative since start
  uint32_t entities = 0;
  uint32_t sessions = 0;
  uint64_t bytesPerSecond = 0;  // game-socket send rate
  uint64_t droppedEntities = 0; // send-budget sheds this window
};
```

Design notes:

- **`AdminEvent.text` is preformatted by the server.** The manager displays
  lines; it doesn't need game knowledge (commodity names, crime tables) to
  render the feed. `kind`/`subject` exist for coloring/filtering and future
  click-through, not for reconstruction.
- **Roster is per-row messages, not one roster blob.** 100 sessions × ~40 bytes
  would overflow a safe UDP payload as a single message; per-row messages let
  `ReliableChannel`'s existing MTU packing do the work, and the same message
  doubles as the on-change delta. Ordering within a lane is guaranteed, so
  replay-then-live is race-free.
- **One `PROTOCOL_VERSION`.** Admin messages version with the same
  `Msg::PROTOCOL_VERSION` as everything else (they are catalog messages);
  a mismatched manager is rejected exactly like a mismatched game client.

### 3.4 Connection lifecycle

```
Manager                                   Server (admin socket)
  │ AdminHello{ver, key}  (token 0) ────────►  key ok? version ok?
  │                                            ├─ no: AdminHelloReject, mute on
  │                                            │      repeated bad keys
  │ ◄──────────── AdminHelloAck{token, ...} ───┤ yes: create admin session
  │ ◄─ Bulk: ring replay (≤256 AdminEvents) ───┤
  │ ◄─ Bulk: roster replay (N AdminPlayerInfo)─┤
  │ ◄─ Gameplay: live deltas + AdminHealth ────┤  ...steady state...
  │ (acks flow back; ack traffic = liveness)   │
  │        silence > ADMIN_TIMEOUT_TICKS ────► │  reap admin session
```

- **Liveness:** the manager's own reliable-lane ack datagrams mark it seen (the
  `MessageEndpoint` emits a datagram whenever it has an ack to deliver, and the
  server is sending health every second, so a live manager always has ack
  traffic). Reap after `ADMIN_TIMEOUT_TICKS` (~30 s) of silence; the manager
  simply re-hellos to reconnect.
- **Multiple managers:** the session table is a map; 2–3 concurrent admins work
  with zero extra design (each gets its own endpoint + replay). A small cap
  (`MAX_ADMIN_SESSIONS = 4`) bounds it.
- **Brute-force defense:** pre-token datagrams on the admin port are
  rate-limited per endpoint (reuse the `RATE_*` pattern from
  `ServerSessions.h:81`), the key comparison is constant-time, and an endpoint
  exceeding `ADMIN_KEY_MAX_FAILURES` (e.g. 5) is muted for
  `ADMIN_MUTE_TICKS` (~60 s).

---

## 4. Server side

### 4.1 `Neuron::Server::AdminChannel` (NeuronServer — new, headless)

The one new server-engine class. Like `ServerSessions` it is **socket-free**
(bytes in / bytes out via each admin session's `MessageEndpoint`), so the whole
protocol — handshake, rejects, mutes, replay, deltas, reaping — is unit-tested
headlessly in `Tests/NeuronServer`.

```cpp
class AdminChannel
{
public:
  // _key: the shared secret (from DSO_ADMIN_KEY). _tokens: CSPRNG source
  // (SecureRandom64 injected, same pattern as ServerSessions::SetTokenSource).
  AdminChannel(std::string _key, std::function<uint64_t()> _tokens);

  // Inbound: an NRLB datagram from the admin socket. Authenticates by token
  // (or handles the hello when token == 0), applies rate limits/mutes.
  void OnDatagram(const Net::Endpoint& _from, const uint8_t* _data, std::size_t _size,
                  uint32_t _tick, const AdminGreeting& _greeting);   // greeting = AdminHelloAck fields

  // Feed points (called by GameServer at its existing hook sites; plain data in).
  void PushEvent(uint32_t _tick, Msg::AdminEventKind _kind, uint32_t _subject, std::string _text);
  void PushRoster(const Msg::AdminPlayerInfo& _row);      // change-gated by the caller
  void PushPlayerGone(uint32_t _playerId, uint8_t _reason);
  void PushHealth(const Msg::AdminHealth& _h);

  // Outbound: every admin session's pending datagrams (send + endpoint pairs),
  // and reap silent sessions. Called once per tick from PublishAdmin.
  void WriteDatagrams(uint32_t _tick, std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>>& _out);

  [[nodiscard]] std::size_t SessionCount() const;
private:
  struct AdminSession { Net::Endpoint endpoint; uint64_t token; Msg::MessageEndpoint events; uint32_t lastSeenTick; };
  // key check (constant-time), per-endpoint pre-auth rate window + failure mute,
  // token -> session map, and the event ring:
  std::array<Msg::AdminEvent, 256> m_ring;  uint32_t m_ringNext = 0;  // sequence source
};
```

Behavioral notes:

- `PushEvent` appends to the ring **always** (even with zero admins connected)
  — that is what makes connect-anytime history work.
- On accept, the new session's endpoint gets the ring (in sequence order) and
  the current roster queued on the **Bulk** lane, so a large backlog can never
  head-of-line-block the live Gameplay lane (`MessageEndpoint` lane isolation,
  already tested).
- The roster **change-gating stays in `GameServer`** (an
  `OnChangeCache<uint32_t, Msg::AdminPlayerInfo>` keyed by playerId, the
  existing pattern from `m_lastStatus`), so `AdminChannel` stays a dumb pipe.

### 4.2 `GameServer` / `Main.cpp` wiring (Server exe)

- **`Main.cpp`:** when `DSO_ADMIN_KEY` is set, open the second `UdpSocket` on
  `ADMIN_PORT` (or its CLI override) and pass it to `GameServer` (which holds a
  pointer; null = feature off, like `m_persist`).
- **Tick integration** — two new, trivially bounded phases:
  - `PumpAdmin()` right after `ReceiveDatagrams()`: `PumpDatagrams` on the admin
    socket with its own small budget (`ADMIN_RECV_BUDGET = 32`) and one route
    (`RELIABLE_MAGIC` → `AdminChannel::OnDatagram`).
  - `PublishAdmin()` at the end of `PublishState()`: flush roster deltas through
    the on-change cache, `PushHealth` every `ADMIN_HEALTH_INTERVAL = 30` ticks
    (~1 s, from a `TickMetrics`-style rolling window), then
    `AdminChannel::WriteDatagrams` → `m_socketAdmin->SendTo`.
- **Event taps** — one `PushEvent` line at each existing site (no new logic):

  | Site (exists today) | AdminEventKind |
  |---|---|
  | hello Accepted / Loading spawn completed | `PlayerJoined` |
  | hello Resumed | `PlayerReconnected` |
  | session reap in roster upkeep | `PlayerLeft` |
  | `OnEntityKilled` | `Kill` |
  | `HandleChat` (post-moderation) | `Chat` |
  | `OnCrime` | `Crime` |
  | `NoteOverrun` / metrics summary with overruns | `TickOverrun` |
  | server boot (constructor) | `ServerStarted` |

- **Config additions** (`ServerConfig.h`): `ADMIN_PORT`, `ADMIN_RECV_BUDGET`,
  `ADMIN_TIMEOUT_TICKS`, `ADMIN_HEALTH_INTERVAL`, `MAX_ADMIN_SESSIONS`,
  `ADMIN_KEY_MAX_FAILURES`, `ADMIN_MUTE_TICKS`, `ADMIN_EVENT_RING = 256`.

Cost when enabled: one extra socket pump (budget 32) and at most a few reliable
datagrams per second per admin. Cost when `DSO_ADMIN_KEY` is unset: **zero** —
no socket, no pushes (a null-check), identical behavior to today.

---

## 5. Manager side

### 5.1 `Neuron::Client::AdminClient` (NeuronClient — new)

The `ReplicationClient` shape without snapshots — a `UdpSocket`, one
`MessageEndpoint`, and a typed mirror the GUI reads:

```cpp
class AdminClient
{
public:
  bool Connect(const Net::Endpoint& _server, const std::string& _adminKey);  // opens + queues AdminHello
  void Disconnect();

  // Per-frame: drain the socket, decode records into the mirror, flush
  // outgoing acks / the unacked hello. Bounded like ReplicationClient::Pump.
  void Pump();

  enum class State { Disconnected, Connecting, Connected, Rejected, TimedOut };
  [[nodiscard]] State GetState() const;
  [[nodiscard]] Msg::AdminRejectReason RejectReason() const;
  [[nodiscard]] const Msg::AdminHelloAck& ServerIdentity() const;

  // The GUI's data: a stable roster snapshot, the event feed (bounded deque,
  // ring sequence-ordered), and a short health history (for meters/trends).
  [[nodiscard]] const std::map<uint32_t, Msg::AdminPlayerInfo>& Roster() const;
  [[nodiscard]] const std::deque<Msg::AdminEvent>& Events() const;      // capped, e.g. 512
  [[nodiscard]] const std::deque<Msg::AdminHealth>& HealthHistory() const;  // capped, e.g. 120 samples
};
```

`Connecting → TimedOut` after N seconds without `AdminHelloAck`;
`Connected → TimedOut` after M seconds without any server datagram (the server
sends health every second, so silence is meaningful). Headless-testable: in
`Tests/NeuronClient`, an `AdminClient`-shaped state machine is driven
byte-for-byte against a real `AdminChannel` — the full protocol round-trips in
a unit test with **no OS sockets** (both ends are bytes-in/bytes-out; only
`Connect/Pump`'s socket edge stays thin). This mirrors how the reliable lanes
are already tested.

### 5.2 `ServerManager/` — the executable

New top-level CMake target (added to the root `CMakeLists.txt` beside
`BotClient`), `add_executable(ServerManager WIN32 ...)`, linking
**NeuronClient** only — the `DeepspaceOutpost` target's structure without the
game sources. No `GameData` copy step is needed beyond the font/strings assets
the GUI stack loads; reuse the same post-build copy pattern for just those
files (verified during SM4).

| File | Content |
|---|---|
| `WinMain.cpp` | Entry point: home dir + DPI awareness + `ClientEngine::Startup(L"DSO Server Manager", ...)` + `StartGame(make_self<ManagerApp>())` + the frame loop (`for (;;) ClientEngine::Frame(33)` until quit) — the `DeepspaceOutpost/WinMain.cpp` shape without `game_main()`. |
| `ManagerApp.h/.cpp` | `ManagerApp : Neuron::GameMain`. `Startup()` registers the windows on `Canvas`; `Update(dt)` calls `m_admin.Pump()` and refreshes window data; `RenderCanvas()` runs the `Canvas` 2D pass (no 3D scene at all). Owns the `AdminClient`. |
| `ConnectWindow.h/.cpp` | `GuiWindow`: IP, port and key fields (`BeginTextEdit`), Connect/Disconnect button, state + reject-reason line. Last-used address/port persisted to `servermanager.cfg` next to the exe (via `FileSys`); the key is **not** persisted in v1. |
| `PlayersWindow.h/.cpp` | `GuiWindow` rendering the roster as text rows via `Render2D`/`TextRenderer` in its `Render()` override (the `GameWindows.cpp` pattern): name, playerId, IP:port, RTT, score, state, connected-for. Row count in the title bar. |
| `EventsWindow.h/.cpp` | Scrolling feed of `AdminEvent` lines, newest at the bottom, kind-colored (kill red, join green, chat white, overrun yellow), auto-scroll unless the user scrolled up (mouse wheel via `MouseEvent`). |
| `HealthWindow.h/.cpp` | Current `AdminHealth` as labeled values plus simple bar meters (avg/max tick ms vs the 33 ms budget) and a text trend from `HealthHistory()`. A red banner when overruns grew this window or no sample arrived for >3 s. |

All four windows are plain `GuiWindow` subclasses on the existing `Canvas` —
movable, focusable, keyboard/mouse handled by the stack already. No new widget
framework; custom drawing stays inside `Render()` overrides.

---

## 6. Security posture (v1)

- **Off by default.** No `DSO_ADMIN_KEY`, no admin socket — the attack surface
  doesn't exist unless deliberately enabled.
- **Secret + token.** The key authenticates the hello (constant-time compare,
  failure mute); the CSPRNG token authenticates everything after, exactly like
  player sessions. The key itself crosses the wire once per connect,
  unencrypted — acceptable for v1 on a LAN/VPN-restricted port, and the reason
  the port is separate: **firewall the admin port** to the operator's network.
  Documented in the doc and the server boot banner.
- **Read-only wire.** v1 has no server-mutating admin message; a compromised
  manager can observe, not act. Commands (§8) arrive only with the token
  discipline already in place, and are the point to consider a real key
  exchange/encryption if the channel must cross untrusted networks.
- **No game-port coupling.** Admin traffic cannot consume the game pump budget,
  and admin session state lives outside `ServerSessions` — a management-channel
  bug cannot corrupt player sessions.
- **IP visibility.** `AdminPlayerInfo` exposes player endpoints to the admin —
  intended (it's the operator's own server), worth stating.

---

## 7. Work plan

Phases are independently buildable and testable, in dependency order. Naming
follows the roadmap's letter-track convention: **SM1–SM5**.

| Phase | Deliverable | Test gate |
|---|---|---|
| **SM1 — Admin wire schema** | `NeuronCore/Messages/Defs/Admin.h`: the eight messages of §3.3, registered ids `0x0F00–0x0F06`, doc comment per message. | `Tests/NeuronCore`: codec round-trip per message (the existing catalog-message test pattern); registry uniqueness already asserted by the message registry tests. |
| **SM2 — AdminChannel (server engine)** | `NeuronServer/AdminChannel.h/.cpp` per §4.1: handshake (accept/reject/version), token auth, pre-auth rate limit + failure mute, event ring + replay, roster/health push, Bulk-lane replay, timeout reap, session cap. | `Tests/NeuronServer`, headless: good/bad key, mute after N failures, token rebind, ring wrap + full replay order, replay-then-live sequencing, reap on silence, multi-admin isolation. |
| **SM3 — Server wiring** | `ServerConfig.h` knobs; `Main.cpp` opens the admin socket when `DSO_ADMIN_KEY` is set; `GameServer::PumpAdmin`/`PublishAdmin` phases; the eight event taps of §4.2; roster on-change cache; health sampling window. | Existing suites stay green with the feature off (no env var). With it on: manual loopback check via SM4's client; `BotClient --smoke` unchanged (doesn't set the key) proves zero default impact. |
| **SM4 — AdminClient + ServerManager exe** | `NeuronClient/AdminClient.h/.cpp`; new `ServerManager/` target per §5.2 with the Connect/Players/Events/Health windows; root `CMakeLists.txt` entry; `servermanager.cfg` persistence of address/port. | `Tests/NeuronClient`: AdminClient⇄AdminChannel byte-level round trip (connect, replay, deltas, timeout) with no sockets. Manual: run `Server` + game client + ServerManager on loopback; join/fly/kill/chat/quit and watch roster + feed + health move. |
| **SM5 — Hardening + docs** | Reconnect UX (auto re-hello with backoff), feed scrollback polish, red no-data banner, boot banner line ("admin channel on :40001"), ARCHITECTURE.md §-entry cross-referencing this doc, AGENTS.md project-table row for ServerManager. | Kill/restart the server under a connected manager: it must show TimedOut, then reconnect and receive the fresh ring (including `ServerStarted`). Soak: manager attached during a `BotClient --bots 100` run; server tick metrics unaffected. |

Estimated wire cost at steady state (1 admin, 100 players): health ~60 B/s,
events bursty but tiny, roster deltas on change only — well under 1 kB/s.

---

## 8. Future work (designed-for, not built)

### 8.1 Admin commands (`0x0F10+`, manager → server, Control lane)

`AdminCommand`-family messages with an `AdminCommandAck{sequence, ok, text}`
reply: `KickPlayer{playerId}`, `Broadcast{text}` (rides the existing Chat relay
as a server-sourced line), `SaveNow` (forces the B4 cadence save),
`GracefulShutdown{seconds}` (announce, final save, exit the `Main.cpp` loop —
which today is `for(;;)` with no exit path; this adds one). Every accepted
command goes through the existing `LogCommand` audit path. The GUI grows
buttons on `PlayersWindow` rows and a commands window; the channel, identity
and framing do not change.

### 8.2 Remote settings

The real precondition is a **runtime server config**: today's
`DSOServer::Cfg::*` are `constexpr`. The path: a `RuntimeConfig` struct
(defaults = today's constants) owned by `GameServer` and threaded to the
consumers that want live tuning (spawn cadence, NPC cap, AOI radius, budgets…),
plus `AdminGetConfig`/`AdminSetConfig{name, value}` messages and a settings
window with typed editors. Persisted overrides would ride the existing DB
(`dbo.server_config`) so a restart keeps them. This is deliberately **after**
commands: read-only → actions → tuning is the trust gradient.

### 8.3 Beyond

- Per-player detail pane (position, cargo, wanted) via an on-demand
  `AdminPlayerDetail` query message — pull, not push, to keep steady-state tiny.
- Health history graphs (tick-ms sparkline) once a line-graph draw helper
  exists on `Render2D`.
- Multiple server profiles in `servermanager.cfg` (a saved-servers list on the
  connect window).
- If the channel must cross untrusted networks: replace the plaintext key with
  a challenge-response (server sends a nonce, manager returns an HMAC) —
  message-level change only, ids already reserved.
