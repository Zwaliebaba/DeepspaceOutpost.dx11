#include "pch.h"

#include "GameLogic.h"

using namespace winrt;
using namespace Neuron;

int main()
{
	printf("Starting DSOServer (GameLogic v%u)...\n", GameLogic::Version());
	CoreEngine::Startup();

	// The authoritative world is an ECS registry that GameLogic ticks. Seed one
	// moving entity so the loop demonstrates the server simulation advancing.
	ECS::Registry world;
	const ECS::EntityId ship = world.Create();
	world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
	world.Add<GameLogic::Velocity>(ship, GameLogic::Velocity{ { 100, 0, 0 } });

	uint64_t ticks = 0;
	bool stop = false;
	while (!stop)
	{
		Timer::Core::Update();

		GameLogic::Tick(world);   // advance the authoritative simulation one tick
		++ticks;

		if (Timer::Core::GetTotalSeconds() > 10)
			stop = true;
	}

	const GameLogic::WorldTransform& t = world.Get<GameLogic::WorldTransform>(ship);
	printf("Ran %llu sim ticks; ship world x = %lld\n",
	       static_cast<unsigned long long>(ticks),
	       static_cast<long long>(t.position.x));
}
