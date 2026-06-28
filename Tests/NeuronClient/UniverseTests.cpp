#include <gtest/gtest.h>

#include "Universe.h"

using namespace Neuron;

TEST(Universe, SpawnCarriesTypeAndTransform)
{
  Universe u;
  EXPECT_TRUE(u.ObjectCount() == 0);

  Game::Transform t;
  t.location.x = 100.0;
  t.location.y = -5.0;
  t.rotX = 3;
  t.distance = 42;

  const ECS::EntityId e = u.Spawn(11, t);   // 11 == SHIP_COBRA3 in shipdata.h

  EXPECT_TRUE(u.ObjectCount() == 1);
  EXPECT_TRUE(u.Reg().Has<Game::ShipType>(e));
  EXPECT_TRUE(u.Reg().Get<Game::ShipType>(e).type == 11);
  EXPECT_TRUE(u.Reg().Has<Game::Transform>(e));
  EXPECT_TRUE(u.Reg().Get<Game::Transform>(e).location.x == 100.0);
  EXPECT_TRUE(u.Reg().Get<Game::Transform>(e).location.y == -5.0);
  EXPECT_TRUE(u.Reg().Get<Game::Transform>(e).rotX == 3);
  EXPECT_TRUE(u.Reg().Get<Game::Transform>(e).distance == 42);
}

TEST(Universe, FlightComponentsAttachAndRead)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId e = u.Spawn(1, t);

  u.Reg().Add<Game::Motion>(e, Game::Motion{ 7, 1 });
  u.Reg().Add<Game::Combat>(e, Game::Combat{ 200, 3, 16 });
  u.Reg().Add<Game::Ai>(e, Game::Ai{ 0, 100 });

  EXPECT_TRUE(u.Reg().Get<Game::Motion>(e).velocity == 7);
  EXPECT_TRUE(u.Reg().Get<Game::Motion>(e).acceleration == 1);
  EXPECT_TRUE(u.Reg().Get<Game::Combat>(e).energy == 200);
  EXPECT_TRUE(u.Reg().Get<Game::Combat>(e).missiles == 3);
  EXPECT_TRUE(u.Reg().Get<Game::Combat>(e).flags == 16);
  EXPECT_TRUE(u.Reg().Get<Game::Ai>(e).bravery == 100);
}

TEST(Universe, PlayerEntityRoundTrips)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId p = u.Spawn(11, t);
  u.Reg().Add<Game::PlayerTag>(p, Game::PlayerTag{});
  u.SetPlayer(p);

  EXPECT_TRUE(u.Player() == p);
  EXPECT_TRUE(u.Reg().IsValid(u.Player()));
  EXPECT_TRUE(u.Reg().Has<Game::PlayerTag>(u.Player()));
}

TEST(Universe, IterateAllObjects)
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
  EXPECT_TRUE(count == 3);
  EXPECT_TRUE(typeSum == 6);
}

TEST(Universe, DestroyRemovesObject)
{
  Universe u;
  Game::Transform t;
  const ECS::EntityId a = u.Spawn(1, t);
  const ECS::EntityId b = u.Spawn(2, t);
  EXPECT_TRUE(u.ObjectCount() == 2);

  u.Destroy(a);
  EXPECT_TRUE(u.ObjectCount() == 1);
  EXPECT_TRUE(!u.Reg().IsValid(a));
  EXPECT_TRUE(u.Reg().IsValid(b));
  EXPECT_TRUE(u.Reg().Get<Game::ShipType>(b).type == 2);
}
