#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A combatant target that never fires back (laser 0, not auto), so a firing
  // test reads a clean "did the shooter hit me" via the energy drop.
  ECS::EntityId Target(ECS::Registry& _w, int64_t _x, int _team, int _energy = 100)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { _x, 0, 0 } });
    _w.Add<Combatant>(e, Combatant{ _team, _energy, /*laser*/ 0, /*range*/ 1, /*autoEngage*/ false });
    return e;
  }

  // The player's own ship stand-in: owned, orderable, fires on command.
  ECS::EntityId Player(ECS::Registry& _w, uint32_t _pid, const Math::Vector3i64& _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<Owner>(e, Owner{ _pid });
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Flight>(e, Flight{});
    _w.Add<FlightIntent>(e, FlightIntent{});
    _w.Add<FlightCaps>(e, FlightCaps{});
    _w.Add<DockState>(e, DockState{});
    _w.Add<Combatant>(e, Combatant{ Team::Player, 255, 10, 6000, /*autoEngage*/ false });
    return e;
  }
}

// --- BuyEscort (the purchase gate) ----------------------------------------------

TEST(Escort, BuyRejectsWhenNotDocked)
{
  Wallet w{ 60000 };
  DockState d{ false, 0 };
  const EquipResult r = BuyEscort(w, d, /*current*/ 0, /*max*/ 4);
  EXPECT_EQ(r.status, Net::StationStatus::NotDocked);
  EXPECT_EQ(w.credits, 60000);   // nothing charged
}

TEST(Escort, BuyRejectsWhenBroke)
{
  Wallet w{ 100 };
  DockState d{ true, 0 };
  const EquipResult r = BuyEscort(w, d, 0, 4);
  EXPECT_EQ(r.status, Net::StationStatus::NotEnoughCredits);
  EXPECT_EQ(w.credits, 100);
}

TEST(Escort, BuyRejectsAtTheEscortCap)
{
  Wallet w{ 1000000 };
  DockState d{ true, 0 };
  const EquipResult r = BuyEscort(w, d, /*current*/ 4, /*max*/ 4);
  EXPECT_EQ(r.status, Net::StationStatus::AlreadyOwned);
  EXPECT_EQ(w.credits, 1000000);   // no charge at the cap
}

TEST(Escort, BuySucceedsAndCharges)
{
  Wallet w{ 60000 };
  DockState d{ true, 0 };
  const EquipResult r = BuyEscort(w, d, 1, 4);
  EXPECT_EQ(r.status, Net::StationStatus::Ok);
  EXPECT_EQ(w.credits, 60000 - ESCORT_FIGHTER_PRICE);
  EXPECT_EQ(r.credits, w.credits);
}

// --- SpawnEscort (the loadout) --------------------------------------------------

TEST(Escort, SpawnGivesAnOwnedOrderedViper)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId esc = SpawnEscort(w, owner, /*slot*/ 0);

  ASSERT_TRUE(w.IsValid(esc));
  // Team::Player, auto-engaging, Viper hull.
  const Combatant& c = w.Get<Combatant>(esc);
  EXPECT_EQ(c.team, Team::Player);
  EXPECT_TRUE(c.autoEngage);
  EXPECT_EQ(w.Get<NetType>(esc).type, ShipType::Viper);
  // Driven by an order, not an AI pilot (so StepOrders owns it, not StepAi).
  EXPECT_FALSE(w.Has<AiPilot>(esc));
  const ActiveOrder& o = w.Get<ActiveOrder>(esc);
  EXPECT_EQ(o.order, Msg::OrderKind::Escort);
  EXPECT_EQ(o.target, owner.index);
  // It flies by the intent pipeline (has the flight components + NPC caps).
  EXPECT_TRUE(w.Has<Flight>(esc));
  EXPECT_TRUE(w.Has<FlightIntent>(esc));
  EXPECT_TRUE(w.Has<FlightCaps>(esc));
}

// --- Escort follow (reuses StepOrders' Escort case) -----------------------------

TEST(Escort, FollowsItsOwnerAndNeverCompletes)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  // Place the escort far ahead of the owner (owner is at the origin facing +z).
  const ECS::EntityId esc = SpawnEscort(w, owner, 0);
  w.Get<WorldTransform>(esc).position = { 0, 0, 50000 };

  const std::vector<ECS::EntityId> fire = StepOrders(w);

  // It is under throttle toward the owner and the Escort order is never "arrived".
  EXPECT_GT(w.Get<FlightIntent>(esc).throttle, 0.0);
  EXPECT_FALSE(w.Get<ActiveOrder>(esc).complete);
  // An escorting (not attacking) unit is not a manual-fire request.
  EXPECT_TRUE(fire.empty());
}

TEST(Escort, HoldsWhenTheOwnerIsGone)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId esc = SpawnEscort(w, owner, 0);
  w.Get<WorldTransform>(esc).position = { 0, 0, 50000 };
  w.Destroy(owner);   // owner despawns

  StepOrders(w);

  const FlightIntent& in = w.Get<FlightIntent>(esc);
  EXPECT_EQ(in.throttle, 0.0);   // holds station: target gone
  EXPECT_TRUE(w.Get<ActiveOrder>(esc).complete);
}

// --- Engagement discipline (StepCombat) -----------------------------------------

TEST(Escort, AutoEngagesPiratesInRange)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId esc = SpawnEscort(w, owner, 0);
  w.Get<WorldTransform>(esc).position = { 0, 0, 0 };
  const ECS::EntityId pirate = Target(w, 1000, Team::Pirate);

  StepCombat(w);

  EXPECT_LT(w.Get<Combatant>(pirate).energy, 100);   // the escort shot the pirate
}

TEST(Escort, DoesNotShootTradersOrPolice)
{
  // Without the escort's target discipline an auto-engaging Team::Player unit would
  // open fire on any other team - including the law and civilian traders.
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId esc = SpawnEscort(w, owner, 0);
  w.Get<WorldTransform>(esc).position = { 0, 0, 0 };
  const ECS::EntityId trader = Target(w, 800, Team::Trader);
  const ECS::EntityId police = Target(w, 1200, Team::Police);

  StepCombat(w);

  EXPECT_EQ(w.Get<Combatant>(trader).energy, 100);   // civilians are spared
  EXPECT_EQ(w.Get<Combatant>(police).energy, 100);   // the law is spared
}

// --- wantsFire gating (escorts fire via StepCombat, players via the command list) --

TEST(Escort, AutoEngageUnitIsNotInTheManualFireList)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId enemy = Target(w, 0, Team::Pirate);   // will be moved ahead
  w.Get<WorldTransform>(enemy).position = { 0, 0, 1000 };   // straight ahead, in range

  // An owned, auto-engaging unit ordered to Attack the enemy: aligned + in range,
  // but it fires through StepCombat, so it must NOT appear in the manual-fire list.
  const ECS::EntityId esc = SpawnEscort(w, owner, 0);
  w.Get<ActiveOrder>(esc) = ActiveOrder{ Msg::OrderKind::Attack, enemy.index, {}, false };

  const std::vector<ECS::EntityId> fire = StepOrders(w);
  for (const ECS::EntityId f : fire)
    EXPECT_NE(f.index, esc.index);
}

TEST(Escort, ManualFireUnitIsInTheFireList)
{
  ECS::Registry w;
  const ECS::EntityId owner = Player(w, 7, { 0, 0, 0 });
  const ECS::EntityId enemy = Target(w, 0, Team::Pirate);
  w.Get<WorldTransform>(enemy).position = { 0, 0, 1000 };

  // The player's own ship (autoEngage=false) ordered to Attack: aligned + in range,
  // so it IS returned for the FireWeapon (command-fire) path.
  w.Add<ActiveOrder>(owner, ActiveOrder{ Msg::OrderKind::Attack, enemy.index, {}, false });

  const std::vector<ECS::EntityId> fire = StepOrders(w);
  bool found = false;
  for (const ECS::EntityId f : fire)
    if (f.index == owner.index) found = true;
  EXPECT_TRUE(found);
}
