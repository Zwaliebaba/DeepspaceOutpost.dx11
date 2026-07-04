#include <gtest/gtest.h>

#include "GameLogic.h"                    // SummarizeStrategic, Team, Combatant, WorldTransform
#include "Messages/Defs/Strategic.h"      // Msg::StrategicSummary / StrategicAlert
#include "Messages/Serialize.h"           // Encode / Decode

using namespace Neuron;

namespace
{
  ECS::EntityId SpawnCombatant(ECS::Registry& _w, int64_t _x, int _team)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, 0, 0 } });
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ _team, 100, 10, 5000 });
    return e;
  }
}

TEST(Strategic, CountsPlayersAsFriendlyAndPiratesAsHostile)
{
  ECS::Registry w;
  SpawnCombatant(w, 100, GameLogic::Team::Player);
  SpawnCombatant(w, 200, GameLogic::Team::Player);
  SpawnCombatant(w, 300, GameLogic::Team::Pirate);
  SpawnCombatant(w, 400, GameLogic::Team::Pirate);
  SpawnCombatant(w, 500, GameLogic::Team::Pirate);

  const GameLogic::StrategicCounts c = GameLogic::SummarizeStrategic(w, Math::Vector3i64{ 0, 0, 0 });
  EXPECT_EQ(c.friendly, 2);
  EXPECT_EQ(c.hostile, 3);
}

TEST(Strategic, IgnoresCombatantsBeyondTheStrategicRadius)
{
  ECS::Registry w;
  SpawnCombatant(w, 1000, GameLogic::Team::Player);                       // near
  SpawnCombatant(w, GameLogic::STRATEGIC_RADIUS + 1, GameLogic::Team::Pirate);   // just outside

  const GameLogic::StrategicCounts c = GameLogic::SummarizeStrategic(w, Math::Vector3i64{ 0, 0, 0 });
  EXPECT_EQ(c.friendly, 1);
  EXPECT_EQ(c.hostile, 0);   // the distant pirate does not count toward this system
}

TEST(Strategic, PoliceTradersAndStationsAreNeutral)
{
  ECS::Registry w;
  SpawnCombatant(w, 100, GameLogic::Team::Police);
  SpawnCombatant(w, 200, GameLogic::Team::Trader);
  SpawnCombatant(w, 300, GameLogic::Team::Station);

  const GameLogic::StrategicCounts c = GameLogic::SummarizeStrategic(w, Math::Vector3i64{ 0, 0, 0 });
  EXPECT_EQ(c.friendly, 0);
  EXPECT_EQ(c.hostile, 0);
}

TEST(Strategic, SummaryMessageRoundTrips)
{
  Msg::StrategicSummary in;
  in.systemId = 42;
  in.friendlyCount = 7;
  in.hostileCount = 3;
  in.alert = static_cast<uint8_t>(Msg::StrategicAlert::UnderAttack);

  Msg::StrategicSummary out;
  ASSERT_TRUE(Msg::Decode(Msg::Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_EQ(out.systemId, 42u);
  EXPECT_EQ(out.alert, static_cast<uint8_t>(Msg::StrategicAlert::UnderAttack));
}
