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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "NetLib.h"
#include "ReplicationClient.h"
#include "Messages/Framing.h"   // Msg::PROTOCOL_VERSION

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
  };

  // Run the bot fleet against `server` for the configured duration. Returns true
  // when every bot connected, streamed snapshots, and completed the chart pull.
  bool RunBots(const Options& _o, const Net::Endpoint& _server)
  {
    std::vector<Bot> bots(static_cast<std::size_t>(_o.bots));
    for (std::size_t i = 0; i < bots.size(); ++i)
    {
      if (!bots[i].rc.Open(/*ephemeral*/ 0))
      {
        printf("bot %zu: socket open failed\n", i);
        return false;
      }
      bots[i].rc.SetServerEndpoint(_server);
      // The hello rides the reliable Control lane and is redelivered by Pump()
      // until the server acks it, so one call here is enough.
      bots[i].rc.SendHello(Msg::PROTOCOL_VERSION, "Bot-" + std::to_string(i + 1));
    }

    const ULONGLONG endMs = GetTickCount64() + static_cast<ULONGLONG>(_o.seconds) * 1000ull;
    int frame = 0;
    for (; GetTickCount64() < endMs; ++frame)
    {
      for (std::size_t i = 0; i < bots.size(); ++i)
      {
        Bot& b = bots[i];
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
        if (b.rc.LatestTick() > 0)
          b.sawSnapshot = true;

        // Deterministic orbit intent: gentle throttle, roll flips on a per-bot
        // phase so the fleet doesn't fly as one blob. Latest-wins server-side.
        Msg::InputCommand in;
        in.sequence = ++b.inputSeq;
        in.throttle = 0.4f;
        in.rollAxis = (((frame + static_cast<int>(i) * 7) / 30) % 2 == 0) ? 0.6f : -0.6f;
        in.pitchAxis = 0.15f;
        b.rc.SendInput(in);

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
      Bot& b = bots[i];
      const bool connected = b.connectFrame >= 0;
      const bool chart = b.rc.GalaxyComplete();
      printf("bot %zu: connected=%s (frame %d) snapshots=%s entities=%zu chart=%s token=%s playerId=%u\n",
             i, connected ? "yes" : "NO", b.connectFrame,
             b.sawSnapshot ? "yes" : "NO", b.rc.Count(),
             chart ? "complete" : "INCOMPLETE",
             b.rc.SessionToken() != 0 ? "yes" : "NO",
             b.rc.PlayerId());
      ok = ok && connected && b.sawSnapshot && chart && b.rc.SessionToken() != 0
              && b.rc.PlayerId() != 0;   // the C identity layer arrived end-to-end
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
