#include "pch.h"

#include "GameLogic.h"
#include "NetLib.h"
#include "SnapshotPacketizer.h"

using namespace winrt;
using namespace Neuron;

int main()
{
	printf("Starting DSOServer (GameLogic v%u)...\n", GameLogic::Version());
	CoreEngine::Startup();

	// Bring up the networking stack and a UDP socket to publish snapshots from.
	// Snapshots go to a client on the loopback for now; with no listener the
	// datagrams are simply dropped, which is fine for the standalone server.
	Net::NetStartup();
	Net::UdpSocket socket;
	const bool netReady = socket.Open();   // unbound sender
	const Net::Endpoint client = Net::MakeEndpoint(127, 0, 0, 1, 50000);

	// The authoritative world is an ECS registry that GameLogic ticks. Seed one
	// steerable ship under full throttle so the loop demonstrates the sim
	// advancing AND the replication path carrying its transform + orientation.
	ECS::Registry world;
	const ECS::EntityId ship = world.Create();
	world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
	world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});
	world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{ 0.0, 0.05, 1.0 });

	uint32_t ticks = 0;
	uint64_t datagrams = 0;
	bool stop = false;
	while (!stop)
	{
		Timer::Core::Update();

		GameLogic::Tick(world);   // advance the authoritative simulation one tick
		++ticks;

		// Serialize the world state and publish it to the client. The snapshot is
		// split into MTU-bounded datagrams, each carrying whole entities and sent
		// fire-and-forget: loss self-heals on the next tick, so there is no
		// reliability layer on this hot path.
		if (netReady)
		{
			const Net::WorldSnapshot snap = GameLogic::BuildWorldSnapshot(world, ticks);
			for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
			{
				if (socket.SendTo(client, datagram.data(), datagram.size()) > 0)
					++datagrams;
			}
		}

		if (Timer::Core::GetTotalSeconds() > 10)
			stop = true;
	}

	const GameLogic::WorldTransform& t = world.Get<GameLogic::WorldTransform>(ship);
	printf("Ran %u sim ticks; sent %llu snapshots; ship world pos = (%lld, %lld, %lld)\n",
	       ticks,
	       static_cast<unsigned long long>(datagrams),
	       static_cast<long long>(t.position.x),
	       static_cast<long long>(t.position.y),
	       static_cast<long long>(t.position.z));

	socket.Close();
	Net::NetShutdown();
}
