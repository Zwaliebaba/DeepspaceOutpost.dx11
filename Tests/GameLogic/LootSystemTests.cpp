#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A wreck that can shed loot: just a position (DropLoot reads only the transform).
  ECS::EntityId SpawnWreck(ECS::Registry& _w, Math::Vector3i64 _pos = { 0, 0, 0 })
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ Team::Pirate, 1, 3, 5000, true });
    return e;
  }

  // A player that can scoop: transform + hold + equipment + dock state, on the
  // Player team so ScoopSystem finds it.
  ECS::EntityId SpawnScooper(ECS::Registry& _w, Math::Vector3i64 _pos,
                             bool _hasScoop, bool _docked = false, int _capacity = 20)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<PlayerTag>(e, PlayerTag{});
    CargoHold hold;
    hold.capacity = _capacity;
    _w.Add<CargoHold>(e, hold);
    Equipment eq;
    eq.fuelScoop = _hasScoop;
    _w.Add<Equipment>(e, eq);
    DockState dock;
    dock.docked = _docked;
    _w.Add<DockState>(e, dock);
    return e;
  }

  // Count the live cargo canisters in the world.
  int CanisterCount(ECS::Registry& _w)
  {
    int n = 0;
    _w.Each<LootItem>([&n](ECS::EntityId, LootItem&) { ++n; });
    return n;
  }
}

TEST(LootSystem, SpawnCanisterHasTheExpectedComponents)
{
  ECS::Registry w;
  const ECS::EntityId c = SpawnCanister(w, { 10, 20, 30 }, { 1, 0, -1 }, /*commodity*/ 4, /*units*/ 3, ShipType::Cargo);

  ASSERT_TRUE(w.Has<LootItem>(c));
  EXPECT_EQ(w.Get<LootItem>(c).commodity, 4);
  EXPECT_EQ(w.Get<LootItem>(c).units, 3);
  EXPECT_EQ(w.Get<LootItem>(c).life, LOOT_LIFE_TICKS);
  EXPECT_EQ(w.Get<WorldTransform>(c).position.x, 10);
  EXPECT_EQ(w.Get<Velocity>(c).perTick.z, -1);
  EXPECT_EQ(w.Get<NetType>(c).type, ShipType::Cargo);
}

TEST(LootSystem, LootMeshForPicksTheModelByCommodity)
{
  EXPECT_EQ(LootMeshFor(ALLOYS_COMMODITY), ShipType::Alloy);
  EXPECT_EQ(LootMeshFor(MINERALS_COMMODITY), ShipType::Rock);
  EXPECT_EQ(LootMeshFor(/*Food*/ 0), ShipType::Cargo);
  EXPECT_EQ(LootMeshFor(/*Computers*/ 7), ShipType::Cargo);
}

TEST(LootSystem, StepLootDespawnsExpiredCanisters)
{
  ECS::Registry w;
  const ECS::EntityId c = SpawnCanister(w, { 0, 0, 0 }, { 0, 0, 0 }, 0, 1, ShipType::Cargo);
  w.Get<LootItem>(c).life = 2;

  EXPECT_EQ(StepLoot(w), 0);   // life 2 -> 1, still alive
  EXPECT_TRUE(w.IsValid(c));
  EXPECT_EQ(StepLoot(w), 1);   // life 1 -> 0, despawned
  EXPECT_FALSE(w.IsValid(c));
}

TEST(LootSystem, DropLootIsANoOpWithoutATransform)
{
  ECS::Registry w;
  const ECS::EntityId ghost = w.Create();   // no WorldTransform
  uint32_t rng = 12345u;
  EXPECT_EQ(DropLoot(w, ghost, rng), 0);
  EXPECT_EQ(CanisterCount(w), 0);
}

TEST(LootSystem, DropLootScattersValidCanistersOverManyKills)
{
  ECS::Registry w;
  uint32_t rng = 0xBEEFu;

  int total = 0;
  for (int i = 0; i < 50; ++i)
  {
    const ECS::EntityId wreck = SpawnWreck(w, { static_cast<int64_t>(i) * 1000, 0, 0 });
    total += DropLoot(w, wreck, rng);
    w.Destroy(wreck);
  }

  EXPECT_GT(total, 0);                    // over 50 kills, SOMETHING drops (deterministic seed)
  EXPECT_EQ(CanisterCount(w), total);     // every reported drop is a live entity

  // Every canister is well-formed: a single unit of a real commodity, full life, and
  // a mesh consistent with its commodity.
  w.Each<LootItem>([&w](ECS::EntityId _id, LootItem& _l)
  {
    EXPECT_GE(_l.commodity, 0);
    EXPECT_LT(_l.commodity, COMMODITY_COUNT);
    EXPECT_EQ(_l.units, 1);
    EXPECT_EQ(_l.life, LOOT_LIFE_TICKS);
    EXPECT_EQ(w.Get<NetType>(_id).type, LootMeshFor(_l.commodity));
  });
}

TEST(LootSystem, DropPlayerCargoSpillsOneCanisterPerStack)
{
  ECS::Registry w;
  const ECS::EntityId player = w.Create();
  w.Add<WorldTransform>(player, WorldTransform{ { 500, 0, 0 } });
  CargoHold hold;
  hold.units[0] = 3;    // Food
  hold.units[9] = 2;    // Alloys
  w.Add<CargoHold>(player, hold);

  uint32_t rng = 7u;
  const int dropped = DropPlayerCargo(w, player, { 500, 0, 0 }, rng);

  EXPECT_EQ(dropped, 2);                 // one canister per non-empty stack
  EXPECT_EQ(CanisterCount(w), 2);

  int foodUnits = 0, alloyUnits = 0;
  w.Each<LootItem>([&](ECS::EntityId, LootItem& _l)
  {
    if (_l.commodity == 0) foodUnits = _l.units;
    if (_l.commodity == 9) alloyUnits = _l.units;
  });
  EXPECT_EQ(foodUnits, 3);               // the whole stack rides one canister
  EXPECT_EQ(alloyUnits, 2);
}

TEST(LootSystem, ScoopWithAScoopAndRoomFillsTheHoldAndConsumesTheCanister)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true);
  const ECS::EntityId can = SpawnCanister(w, { 100, 0, 0 }, { 0, 0, 0 }, /*Food*/ 0, /*units*/ 2, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(changed[0], player.index);
  EXPECT_EQ(w.Get<CargoHold>(player).units[0], 2);   // scooped into the hold
  EXPECT_FALSE(w.IsValid(can));                       // canister consumed
}

TEST(LootSystem, ContactWithoutAScoopSmashesTheCanisterWithoutScooping)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ false);
  const ECS::EntityId can = SpawnCanister(w, { 100, 0, 0 }, { 0, 0, 0 }, 0, 2, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  EXPECT_TRUE(changed.empty());                       // nothing scooped
  EXPECT_EQ(w.Get<CargoHold>(player).units[0], 0);    // hold unchanged
  EXPECT_FALSE(w.IsValid(can));                       // but contact still destroys it
}

TEST(LootSystem, ACanisterOutOfRangeIsLeftAlone)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true);
  const ECS::EntityId can = SpawnCanister(w, { LOOT_SCOOP_RANGE + 1, 0, 0 }, { 0, 0, 0 }, 0, 1, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  EXPECT_TRUE(changed.empty());
  EXPECT_TRUE(w.IsValid(can));                         // untouched
  EXPECT_EQ(w.Get<CargoHold>(player).units[0], 0);
}

TEST(LootSystem, AFullHoldSmashesTonnageLootInsteadOfScoopingIt)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true, /*docked*/ false, /*capacity*/ 20);
  w.Get<CargoHold>(player).units[0] = 20;   // hold full of a tonnage good
  const ECS::EntityId can = SpawnCanister(w, { 50, 0, 0 }, { 0, 0, 0 }, /*Food*/ 0, /*units*/ 1, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  EXPECT_TRUE(changed.empty());
  EXPECT_EQ(w.Get<CargoHold>(player).units[0], 20);   // no room -> not scooped
  EXPECT_FALSE(w.IsValid(can));                        // smashed on contact
}

TEST(LootSystem, NonTonnageLootIsScoopedEvenWithAHoldFullOfTonnage)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true, /*docked*/ false, /*capacity*/ 20);
  w.Get<CargoHold>(player).units[0] = 20;   // tonnage hold is full...
  const ECS::EntityId can = SpawnCanister(w, { 50, 0, 0 }, { 0, 0, 0 }, /*Gold*/ 13, /*units*/ 4, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(w.Get<CargoHold>(player).units[13], 4);   // ...but gold doesn't count against it
  EXPECT_FALSE(w.IsValid(can));
}

TEST(LootSystem, ADockedPlayerDoesNotScoop)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true, /*docked*/ true);
  const ECS::EntityId can = SpawnCanister(w, { 100, 0, 0 }, { 0, 0, 0 }, 0, 1, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  EXPECT_TRUE(changed.empty());
  EXPECT_TRUE(w.IsValid(can));                         // a docked player ignores loot entirely
  EXPECT_EQ(w.Get<CargoHold>(player).units[0], 0);
}

TEST(LootSystem, OneCanisterIsClaimedByASinglePlayer)
{
  ECS::Registry w;
  const ECS::EntityId a = SpawnScooper(w, { 0, 0, 0 }, /*hasScoop*/ true);
  const ECS::EntityId b = SpawnScooper(w, { 100, 0, 0 }, /*hasScoop*/ true);
  const ECS::EntityId can = SpawnCanister(w, { 50, 0, 0 }, { 0, 0, 0 }, /*Food*/ 0, /*units*/ 1, ShipType::Cargo);

  const std::vector<uint32_t> changed = ScoopSystem(w);

  EXPECT_EQ(changed.size(), 1u);                       // exactly one player got it
  EXPECT_FALSE(w.IsValid(can));                        // consumed once
  const int total = w.Get<CargoHold>(a).units[0] + w.Get<CargoHold>(b).units[0];
  EXPECT_EQ(total, 1);                                 // and only one unit entered a hold
}
