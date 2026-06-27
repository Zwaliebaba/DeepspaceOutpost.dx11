#include "TestFramework.h"

#include "Universe.h"

using namespace Neuron;

TEST(Universe_SpawnCarriesTypeAndTransform)
{
  Universe u;
  CHECK(u.ObjectCount() == 0);

  Game::Transform t;
  t.location.x = 100.0;
  t.location.y = -5.0;
  t.rotX = 3;
  t.distance = 42;

  const ECS::EntityId e = u.Spawn(11, t);   // 11 == SHIP_COBRA3 in shipdata.h

  CHECK(u.ObjectCount() == 1);
  CHECK(u.Reg().Has<Game::ShipType>(e));
  CHECK(u.Reg().Get<Game::ShipType>(e).type == 11);
  CHECK(u.Reg().Has<Game::Transform>(e));
  CHECK(u.Reg().Get<Game::Transform>(e).location.x == 100.0);
  CHECK(u.Reg().Get<Game::Transform>(e).location.y == -5.0);
  CHECK(u.Reg().Get<Game::Transform>(e).rotX == 3);
  CHECK(u.Reg().Get<Game::Transform>(e).distance == 42);
}

TEST(Universe_FlightComponentsAttachAndRead)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId e = u.Spawn(1, t);

  u.Reg().Add<Game::Motion>(e, Game::Motion{ 7, 1 });
  u.Reg().Add<Game::Combat>(e, Game::Combat{ 200, 3, 16 });
  u.Reg().Add<Game::Ai>(e, Game::Ai{ 0, 100 });

  CHECK(u.Reg().Get<Game::Motion>(e).velocity == 7);
  CHECK(u.Reg().Get<Game::Motion>(e).acceleration == 1);
  CHECK(u.Reg().Get<Game::Combat>(e).energy == 200);
  CHECK(u.Reg().Get<Game::Combat>(e).missiles == 3);
  CHECK(u.Reg().Get<Game::Combat>(e).flags == 16);
  CHECK(u.Reg().Get<Game::Ai>(e).bravery == 100);
}

TEST(Universe_PlayerEntityRoundTrips)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId p = u.Spawn(11, t);
  u.Reg().Add<Game::PlayerTag>(p, Game::PlayerTag{});
  u.SetPlayer(p);

  CHECK(u.Player() == p);
  CHECK(u.Reg().IsValid(u.Player()));
  CHECK(u.Reg().Has<Game::PlayerTag>(u.Player()));
}

TEST(Universe_IterateAllObjects)
{
  Universe u;
  Game::Transform t;
  u.Spawn(1, t);
  u.Spawn(2, t);
  u.Spawn(3, t);

  int count = 0;
  int typeSum = 0;
  u.Reg().Each<Game::ShipType>([&](ECS::EntityId, Game::ShipType& s)
  {
    ++count;
    typeSum += s.type;
  });
  CHECK(count == 3);
  CHECK(typeSum == 6);
}

TEST(Universe_DestroyRemovesObject)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId a = u.Spawn(1, t);
  const ECS::EntityId b = u.Spawn(2, t);
  CHECK(u.ObjectCount() == 2);

  u.Destroy(a);
  CHECK(u.ObjectCount() == 1);
  CHECK(!u.Reg().IsValid(a));
  CHECK(u.Reg().IsValid(b));
  CHECK(u.Reg().Get<Game::ShipType>(b).type == 2);
}
