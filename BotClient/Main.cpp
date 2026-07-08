// BotClient - the headless load/soak harness (D5, roadmap #20).
//
// Drives N scripted client sessions over the REAL UDP stack - each bot is a full
// ReplicationClient (the production client's network endpoint: hello/handshake,
// session token, reliable lanes, snapshot interpolation, galaxy chart pull) with
// no D3D/audio/window anywhere near it. Bots fly deterministic orbit intents and
// assert the server actually served them: handshake acknowledged, snapshots
// flowing, the full galaxy chart delivered.
//
// Two ways to run:
//   BotClient --bots 8 --seconds 20 --port 40000            against a running server
//   BotClient --smoke --server-exe <path\Server.exe> ...    spawns the server itself,
//     parses its [metrics] lines afterwards (tick overruns), and is registered as
//     the CI smoke test - server + 8 bots on loopback, asserting every bot
//     connects, streams, and completes the chart pull, with tick overruns under a
//     small tolerance (CI runners stall; a hard zero would be flaky).
//
// The 100-bot soak is this same binary with bigger numbers, run manually per the
// plan (it gates entity-cap increases). Per-bot RTT/loss instrumentation arrives
// with E1's Ping/Pong; until then the harness reports frames-to-connect and
// snapshot progress, which loopback CI can assert meaningfully.
//
// Adverse-condition lane: --churn-seconds N forces every bot through one full
// reconnect (a brand-new ReplicationClient / session, i.e. real socket teardown and
// re-handshake over live UDP) partway through the run; the verdict then requires
// each bot to recover - reconnect and resume an advancing authoritative snapshot
// stream with a fresh identity. (The galaxy chart re-pull is best-effort here: the
// pre-churn sessions linger under their grace window and briefly ~double server
// load, so that bulk transfer may not finish in the remaining window - the plain
// lane is what pins chart completion.) Registered as the BotClient.Smoke.Churn CTest.
// (Deliberately-still-manual: artificial packet loss/reorder needs a drop hook in
// the NeuronCore UdpSocket - a change to the shared net stack - so it is not wired
// in here yet; run it from a Windows dev box where CI + local runs can confirm the
// reliability layer recovers.)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <cmath>

#include "NetLib.h"
#include "ReplicationClient.h"
#include "Messages/Framing.h"       // Msg::PROTOCOL_VERSION
#include "Messages/Defs/UnitOrder.h"   // Msg::UnitOrder (the order bot, I1)

using namespace Neuron;

namespace
{
  struct Options
  {
    int bots = 8;
    int seconds = 20;
    uint16_t port = 40000;
    bool smoke = false;
    std::string serverExe;      // --smoke: the server binary to spawn
    uint64_t maxOverruns = 3;   // --smoke: tolerated tick overruns (CI runners stall)
    int churnSeconds = 0;       // >0: each bot force-reconnects (fresh session) once, ~this many seconds in
  };

  Options Parse(int _argc, char** _argv)
  {
    Options o;
    for (int i = 1; i < _argc; ++i)
    {
      const std::string a = _argv[i];
      auto next = [&]() -> const char* { return (i + 1 < _argc) ? _argv[++i] : ""; };
      if (a == "--bots")              o.bots = atoi(next());
      else if (a == "--seconds")      o.seconds = atoi(next());
      else if (a == "--port")         o.port = static_cast<uint16_t>(atoi(next()));
      else if (a == "--smoke")        o.smoke = true;
      else if (a == "--server-exe")   o.serverExe = next();
      else if (a == "--max-overruns") o.maxOverruns = static_cast<uint64_t>(atoll(next()));
      else if (a == "--churn-seconds") o.churnSeconds = atoi(next());
    }
    if (o.bots < 1) o.bots = 1;
    if (o.seconds < 1) o.seconds = 1;
    return o;
  }

  struct Bot
  {
    Client::ReplicationClient rc;
    uint32_t inputSeq = 0;
    int connectFrame = -1;   // frame the HelloAck landed (-1 = not yet)
    bool sawSnapshot = false;
    int lastOrderFrame = -1000;  // I1: throttle how often the order bot re-orders
    uint32_t firstTick = 0;      // first snapshot tick observed (advance check)
    uint32_t lastTick = 0;       // most recent snapshot tick observed
    bool haveFirstTick = false;
    int reconnects = 0;          // churn: times this bot was dropped + re-handshook
  };

  // Bind a fresh bot's socket, point it at the server, and send its hello. The hello
  // rides the reliable Control lane and is redelivered by Pump() until acked, so one
  // call is enough. Shared by initial connect and the churn reconnect.
  bool OpenAndHello(Bot& _b, std::size_t _i, const Net::Endpoint& _server)
  {
    if (!_b.rc.Open(/*ephemeral*/ 0))
    {
      printf("bot %zu: socket open failed\n", _i);
      return false;
    }
    _b.rc.SetServerEndpoint(_server);
    _b.rc.SendHello(Msg::PROTOCOL_VERSION, "Bot-" + std::to_string(_i + 1));
    return true;
  }

  // Run the bot fleet against `server` for the configured duration. Returns true
  // when every bot connected, streamed advancing snapshots, and completed the chart
  // pull. With churnSeconds > 0 each bot is forced through one full reconnect (a
  // brand-new ReplicationClient / session) partway through, and must recover to the
  // same verdict - exercising real socket teardown + re-handshake + a fresh galaxy
  // pull over the live UDP stack, which the deterministic gtest suites cannot.
  bool RunBots(const Options& _o, const Net::Endpoint& _server)
  {
    // Bots are heap-owned so a reconnect can drop one and construct a fresh session
    // in its place (ReplicationClient owns a UdpSocket and is intentionally
    // non-movable, so the fleet cannot live in a value vector across a reconnect).
    const std::size_t count = static_cast<std::size_t>(_o.bots);
    std::vector<std::unique_ptr<Bot>> bots;
    bots.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
      bots.push_back(std::make_unique<Bot>());
      if (!OpenAndHello(*bots[i], i, _server))
        return false;
    }

    const ULONGLONG startMs = GetTickCount64();
    const ULONGLONG endMs = startMs + static_cast<ULONGLONG>(_o.seconds) * 1000ull;
    int frame = 0;
    for (; GetTickCount64() < endMs; ++frame)
    {
      const ULONGLONG now = GetTickCount64();
      for (std::size_t i = 0; i < bots.size(); ++i)
      {
        // Churn: force a single reconnect once we pass the scheduled moment. The
        // drops are staggered per bot (100 ms apart) so the fleet does not all
        // re-handshake on the same server tick.
        if (_o.churnSeconds > 0 && bots[i]->reconnects == 0 &&
            now >= startMs + static_cast<ULONGLONG>(_o.churnSeconds) * 1000ull + i * 100ull)
        {
          bots[i]->rc.Close();                 // real teardown of the live session
          bots[i] = std::make_unique<Bot>();   // fresh, clean-state client
          bots[i]->reconnects = 1;
          if (!OpenAndHello(*bots[i], i, _server))
            return false;
          printf("bot %zu: forced reconnect (fresh session)\n", i);
          continue;   // re-handshake begins next frame
        }

        Bot& b = *bots[i];
        b.rc.Pump();

        if (b.rc.HelloRejected())
        {
          printf("bot %zu: hello REJECTED (protocol mismatch?)\n", i);
          return false;
        }
        if (b.rc.LocalPlayer() == 0xFFFFFFFFu)
          continue;   // still waiting for HelloAck (the hello resends itself)

        if (b.connectFrame < 0)
          b.connectFrame = frame;
        const uint32_t tick = b.rc.LatestTick();
        if (tick > 0)
        {
          b.sawSnapshot = true;
          if (!b.haveFirstTick) { b.firstTick = tick; b.haveFirstTick = true; }
          b.lastTick = tick;
        }

        // Heartbeat only (protocol v4: InputCommand IS just the heartbeat):
        // movement comes from orders. The command carries the sequence + the E2b
        // snapshot ack, and feeds the server's safe-park silence detection, so
        // keep sending it every frame.
        Msg::InputCommand in;
        in.sequence = ++b.inputSeq;
        b.rc.SendInput(in);

        // Order bot (I1): drive movement by UnitOrder{Move}, re-issued periodically to
        // a point that walks around a circle so the fleet perpetually flies and each
        // ship exercises the arrive-and-re-order loop through the real order path.
        // unitId is our own primary entity (LocalPlayer); the point stays well within
        // the server's Move clamp of the spawn region.
        if (frame - b.lastOrderFrame >= 90)   // ~every 1.35s at Sleep(15)
        {
          b.lastOrderFrame = frame;
          const double ang = (static_cast<double>(frame) / 90.0 + static_cast<double>(i))
                           * 1.0471975512;   // 60deg per re-order, phase-offset per bot
          Msg::UnitOrder ord;
          ord.unitId = b.rc.LocalPlayer();
          ord.order = Msg::OrderKind::Move;
          ord.targetX = static_cast<int64_t>(std::cos(ang) * 40000.0);
          ord.targetY = static_cast<int64_t>(std::sin(ang) * 40000.0);
          ord.targetZ = 0;
          b.rc.SendUnitOrder(ord);
        }

        // Drain app events so the reliable queues never back up.
        Net::ReliableMessage msg;
        while (b.rc.PollEvent(msg)) {}
      }
      Sleep(15);   // ~2 pumps per 30 Hz server tick
    }

    // Report + verdict.
    bool ok = true;
    for (std::size_t i = 0; i < bots.size(); ++i)
    {
      Bot& b = *bots[i];
      const bool connected = b.connectFrame >= 0;
      const bool chart = b.rc.GalaxyComplete();
      // The snapshot stream must have ADVANCED, not merely produced a single frame:
      // a server that froze after one tick would pass the old sawSnapshot check.
      const bool advanced = b.haveFirstTick && b.lastTick > b.firstTick;
      printf("bot %zu: connected=%s (frame %d) snapshots=%s advanced=%s entities=%zu chart=%s token=%s playerId=%u reconnects=%d\n",
             i, connected ? "yes" : "NO", b.connectFrame,
             b.sawSnapshot ? "yes" : "NO", advanced ? "yes" : "NO", b.rc.Count(),
             chart ? "complete" : "INCOMPLETE",
             b.rc.SessionToken() != 0 ? "yes" : "NO",
             b.rc.PlayerId(), b.reconnects);

      // Core recovery signal: the session is up, identified (C layer), and the
      // authoritative stream is flowing AND advancing.
      const bool recovered = connected && b.sawSnapshot && advanced
                          && b.rc.SessionToken() != 0 && b.rc.PlayerId() != 0;
      // A steady-state connect must also complete the galaxy chart pull. In the
      // churn lane the post-reconnect re-pull is a full bulk transfer that may not
      // finish in the remaining window (the lingering pre-churn sessions briefly
      // ~double server load), so there the verdict is that the forced reconnect
      // happened and the session fully recovered - the property this lane exists to
      // prove - with the chart re-pull treated as best-effort.
      const bool botOk = (_o.churnSeconds > 0)
                       ? (recovered && b.reconnects > 0)
                       : (recovered && chart);
      ok = ok && botOk;
      b.rc.Close();
    }
    return ok;
  }

  // --- smoke mode: own the server process ---------------------------------------

  struct ServerProcess
  {
    PROCESS_INFORMATION pi{};
    HANDLE logWrite = nullptr;
    std::string logPath;
    bool running = false;
  };

  bool StartServer(const Options& _o, ServerProcess& _sp)
  {
    char tempDir[MAX_PATH];
    GetTempPathA(static_cast<DWORD>(sizeof(tempDir)), tempDir);
    _sp.logPath = std::string(tempDir) + "dso_smoke_server_" + std::to_string(GetCurrentProcessId()) + ".log";

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };   // inheritable log handle
    _sp.logWrite = CreateFileA(_sp.logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (_sp.logWrite == INVALID_HANDLE_VALUE)
      return false;

    std::string cmd = "\"" + _o.serverExe + "\" " + std::to_string(_o.port);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = _sp.logWrite;
    si.hStdError = _sp.logWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &_sp.pi))
    {
      printf("smoke: failed to start '%s' (err %lu)\n", _o.serverExe.c_str(), GetLastError());
      CloseHandle(_sp.logWrite);
      return false;
    }
    _sp.running = true;
    return true;
  }

  void StopServer(ServerProcess& _sp)
  {
    if (!_sp.running)
      return;
    TerminateProcess(_sp.pi.hProcess, 0);
    WaitForSingleObject(_sp.pi.hProcess, 5000);
    CloseHandle(_sp.pi.hThread);
    CloseHandle(_sp.pi.hProcess);
    CloseHandle(_sp.logWrite);
    _sp.running = false;
  }

  // Sum the overruns across every [metrics] line the server printed, and surface
  // the last line for the CI log. Returns false if no metrics line ever appeared
  // (the server never reached a metrics window -> something is very wrong).
  bool ReadOverruns(const std::string& _logPath, uint64_t& _totalOverruns)
  {
    FILE* f = nullptr;
    if (fopen_s(&f, _logPath.c_str(), "rb") != 0 || f == nullptr)
      return false;

    _totalOverruns = 0;
    bool sawMetrics = false;
    char line[512];
    std::string lastMetrics;
    while (fgets(line, sizeof(line), f) != nullptr)
    {
      const char* m = strstr(line, "[metrics]");
      if (m == nullptr)
        continue;
      sawMetrics = true;
      lastMetrics = m;
      if (const char* ov = strstr(m, "overruns="))
        _totalOverruns += strtoull(ov + 9, nullptr, 10);
    }
    fclose(f);
    if (sawMetrics)
      printf("smoke: last server metrics: %s", lastMetrics.c_str());
    return sawMetrics;
  }
}

int main(int argc, char** argv)
{
  const Options o = Parse(argc, argv);

  if (!Net::NetStartup())
  {
    printf("winsock startup failed\n");
    return 1;
  }

  ServerProcess sp;
  if (o.smoke)
  {
    if (o.serverExe.empty() || !StartServer(o, sp))
    {
      printf("smoke: no server to run (--server-exe required)\n");
      return 1;
    }
    printf("smoke: server pid %lu on port %u, log %s\n",
           sp.pi.dwProcessId, static_cast<unsigned>(o.port), sp.logPath.c_str());
  }

  printf("BotClient: %d bots vs 127.0.0.1:%u for %ds\n", o.bots, static_cast<unsigned>(o.port), o.seconds);
  const Net::Endpoint server = Net::MakeEndpoint(127, 0, 0, 1, o.port);
  const bool botsOk = RunBots(o, server);

  bool metricsOk = true;
  if (o.smoke)
  {
    StopServer(sp);
    uint64_t overruns = 0;
    if (!ReadOverruns(sp.logPath, overruns))
    {
      printf("smoke: server produced no [metrics] lines\n");
      metricsOk = false;
    }
    else if (overruns > o.maxOverruns)
    {
      printf("smoke: %llu tick overruns exceeds the tolerance of %llu\n",
             static_cast<unsigned long long>(overruns),
             static_cast<unsigned long long>(o.maxOverruns));
      metricsOk = false;
    }
    else
    {
      printf("smoke: %llu tick overruns (tolerance %llu)\n",
             static_cast<unsigned long long>(overruns),
             static_cast<unsigned long long>(o.maxOverruns));
    }
  }

  Net::NetShutdown();
  const bool ok = botsOk && metricsOk;
  printf("BotClient: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
