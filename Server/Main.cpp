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

int main()
{
  printf("Starting DSOServer (GameLogic v%u) on UDP %u...\n",
         GameLogic::Version(), static_cast<unsigned>(DSOServer::Cfg::SERVER_PORT));
  CoreEngine::Startup();

  Net::NetStartup();
  Net::UdpSocket socket;
  if (!socket.Open(DSOServer::Cfg::SERVER_PORT))
  {
    printf("Failed to bind UDP %u\n", static_cast<unsigned>(DSOServer::Cfg::SERVER_PORT));
    return 1;
  }

  DSOServer::GameServer server(socket);

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
