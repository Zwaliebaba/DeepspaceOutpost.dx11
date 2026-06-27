#include "pch.h"

#include "GameLogic.h"
#include "NetLib.h"
#include "SnapshotPacketizer.h"
#include "GameEvents.h"

using namespace winrt;
using namespace Neuron;

namespace
{
  constexpr uint16_t kServerPort = 40000;                 // we bind here to receive acks
  constexpr int64_t kAoiCellSize = 100000;                // interest-management cell size
  constexpr int kAoiRadiusCells = 1;                      // viewers see +/- 1 cell

  // Gather the indices of every entity currently in the world (for despawn diff).
  std::vector<uint32_t> CurrentIds(ECS::Registry& _world)
  {
    std::vector<uint32_t> ids;
    _world.Each<GameLogic::WorldTransform>([&ids](ECS::EntityId _id, GameLogic::WorldTransform&)
    {
      ids.push_back(_id.index);
    });
    return ids;
  }
}

int main()
{
  printf("Starting DSOServer (GameLogic v%u)...\n", GameLogic::Version());
  CoreEngine::Startup();

  // Bind a UDP socket so we can both publish snapshots AND receive client acks.
  Net::NetStartup();
  Net::UdpSocket socket;
  const bool netReady = socket.Open(kServerPort);
  const Net::Endpoint client = Net::MakeEndpoint(127, 0, 0, 1, 50000);

  // Authoritative world: a steerable player plus a couple of static props.
  ECS::Registry world;
  const ECS::EntityId player = world.Create();
  world.Add<GameLogic::WorldTransform>(player, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(player, GameLogic::Flight{});
  world.Add<GameLogic::FlightIntent>(player, GameLogic::FlightIntent{ 0.0, 0.02, 1.0 });

  const ECS::EntityId propA = world.Create();
  world.Add<GameLogic::WorldTransform>(propA, GameLogic::WorldTransform{ { 5000, 0, 0 } });
  ECS::EntityId propB = world.Create();
  world.Add<GameLogic::WorldTransform>(propB, GameLogic::WorldTransform{ { 0, 5000, 0 } });

  GameLogic::AreaOfInterest aoi(kAoiCellSize);
  GameLogic::DespawnTracker despawns;
  Net::ReliableChannel events;        // reliable event stream to the client

  uint32_t ticks = 0;
  uint64_t datagrams = 0;
  bool stop = false;
  uint8_t recvBuffer[2048];
  while (!stop)
  {
    Timer::Core::Update();

    GameLogic::Tick(world);
    ++ticks;

    // Demonstrate a despawn mid-run: destroy a prop, which the tracker will pick
    // up and turn into a reliable EntityDespawn event.
    if (ticks == 300 && world.IsValid(propB))
      world.Destroy(propB);

    // Reliable events: any entity that vanished this tick despawns.
    for (uint32_t goneId : despawns.Update(CurrentIds(world)))
      Net::SendDespawn(events, goneId);

    if (netReady)
    {
      // Per-viewer area-of-interest snapshot (here: the one player), split into
      // MTU-bounded fire-and-forget datagrams.
      const Math::Vector3i64 viewerPos = world.Get<GameLogic::WorldTransform>(player).position;
      aoi.Rebuild(world);
      const Net::WorldSnapshot snap = aoi.SnapshotFor(world, ticks, viewerPos, kAoiRadiusCells);
      for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
      {
        if (socket.SendTo(client, datagram.data(), datagram.size()) > 0)
          ++datagrams;
      }

      // Reliable event packet (resent until the client acks it).
      const std::vector<uint8_t> eventPacket = events.WritePacket();
      socket.SendTo(client, eventPacket.data(), eventPacket.size());

      // Drain inbound datagrams: route client acks into the reliable channel.
      Net::Endpoint from;
      for (int i = 0; i < 64; ++i)
      {
        const int got = socket.RecvFrom(recvBuffer, sizeof(recvBuffer), from);
        if (got <= 0)
          break;
        if (Net::PeekMagic(recvBuffer, static_cast<std::size_t>(got)) == Net::EVENT_MAGIC)
          events.ReadPacket(recvBuffer, static_cast<std::size_t>(got));
      }
    }

    if (Timer::Core::GetTotalSeconds() > 10)
      stop = true;
  }

  const GameLogic::WorldTransform& t = world.Get<GameLogic::WorldTransform>(player);
  printf("Ran %u ticks; sent %llu snapshot datagrams; %zu events still unacked; player x=%lld\n",
         ticks,
         static_cast<unsigned long long>(datagrams),
         events.PendingOutgoing(),
         static_cast<long long>(t.position.x));

  socket.Close();
  Net::NetShutdown();
}
