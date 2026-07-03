#include "pch.h"

// DSOServer entry point: process startup + the fixed-tick loop. Everything else
// lives in its own piece - tuning in ServerConfig.h, world bootstrap in
// WorldBuilder, the tick pipeline and combat subscribers in GameServer, and the
// generic engine plumbing (datagram pump, on-change cache) in NeuronServer.

#include "GameLogic.h"      // GameLogic::Version banner
#include "NetLib.h"

#include "GameServer.h"
#include "ServerConfig.h"

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

  for (;;)
  {
    Timer::Core::Update();
    server.RunTick();
    Sleep(DSOServer::Cfg::TICK_SLEEP_MS);   // ~30 Hz fixed tick
  }
}
