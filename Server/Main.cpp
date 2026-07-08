#include "pch.h"

// DSOServer entry point: process startup + the fixed-tick loop. Everything else
// lives in its own piece - tuning in ServerConfig.h, world bootstrap in
// WorldBuilder, the tick pipeline and combat subscribers in GameServer, and the
// generic engine plumbing (datagram pump, on-change cache) in NeuronServer.

#include "GameLogic.h"      // GameLogic::Version banner
#include "NetLib.h"

#include "GameServer.h"
#include "ServerConfig.h"
#include "TickPacer.h"      // D3: fixed-timestep accumulator

using namespace winrt;
using namespace Neuron;

int main(int argc, char** argv)
{
  // Optional argv[1] overrides the UDP port (the D5 BotClient smoke runs the
  // server on a side port so it can never collide with a real instance).
  uint16_t port = DSOServer::Cfg::SERVER_PORT;
  if (argc > 1)
  {
    const int p = atoi(argv[1]);
    if (p > 0 && p <= 65535)
      port = static_cast<uint16_t>(p);
  }

  printf("Starting DSOServer (GameLogic v%u) on UDP %u...\n",
         GameLogic::Version(), static_cast<unsigned>(port));
  CoreEngine::Startup();

  Net::NetStartup();
  Net::UdpSocket socket;
  if (!socket.Open(port))
  {
    printf("Failed to bind UDP %u\n", static_cast<unsigned>(port));
    return 1;
  }

  // sm.md ServerManager management channel: a SEPARATE UDP socket on the game port + 1,
  // opened only when DSO_ADMIN_KEY is set (else the feature is entirely off). The key
  // authenticates a manager's hello; keep this port firewalled to the operator's
  // network - the shared secret crosses it in the clear on connect (sm.md §6).
  Net::UdpSocket adminSocket;
  Net::UdpSocket* adminSocketPtr = nullptr;
  const char* adminKey = std::getenv("DSO_ADMIN_KEY");
  if (adminKey != nullptr && adminKey[0] != '\0')
  {
    const uint16_t adminPort = static_cast<uint16_t>(port + 1);
    if (adminSocket.Open(adminPort))
    {
      adminSocketPtr = &adminSocket;
      printf("ServerManager admin channel on UDP %u (keep firewalled to the operator network)\n",
             static_cast<unsigned>(adminPort));
    }
    else
    {
      printf("Failed to bind admin UDP %u; management channel DISABLED\n",
             static_cast<unsigned>(adminPort));
    }
  }

  DSOServer::GameServer server(socket, adminSocketPtr, port);

  // D3: a fixed-timestep accumulator instead of a bare Sleep. Each iteration we
  // measure the real elapsed time, run as many fixed ticks as it earned (bounded,
  // so a stall can't spiral into a burst of catch-up ticks), and sleep the
  // remainder when we are ahead.
  Server::TickPacer pacer(static_cast<double>(DSOServer::Cfg::TICK_SLEEP_MS), DSOServer::Cfg::TICK_MAX_CATCHUP);
  for (;;)
  {
    Timer::Core::Update();
    const double elapsedMs = static_cast<double>(Timer::Core::GetElapsedSeconds()) * 1000.0;

    bool overran = false;
    const int ticks = pacer.Pump(elapsedMs, overran);
    for (int i = 0; i < ticks; ++i)
      server.RunTick();
    if (overran)
      server.NoteOverrun();

    const double sleepMs = pacer.SleepMs();
    if (sleepMs > 1.0)
      Sleep(static_cast<DWORD>(sleepMs));
  }
}
