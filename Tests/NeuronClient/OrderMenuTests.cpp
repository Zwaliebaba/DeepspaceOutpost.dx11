#include <gtest/gtest.h>

#include "input/OrderMenu.h"

using namespace Neuron::Input;
using Neuron::Msg::OrderKind;

namespace
{
  bool MenuHas(const MenuOptions& _m, OrderKind _o)
  {
    for (std::size_t i = 0; i < _m.count; ++i)
      if (!_m.items[i].isInfo && _m.items[i].order == _o) return true;
    return false;
  }
  bool MenuHasInfo(const MenuOptions& _m)
  {
    for (std::size_t i = 0; i < _m.count; ++i)
      if (_m.items[i].isInfo) return true;
    return false;
  }
}

TEST(OrderMenu, ClassifyMapsEntityClassesToTargets)
{
  EXPECT_EQ(ClassifyTarget(EntityClass::None,     false, false, false, false), TargetKind::Empty);
  EXPECT_EQ(ClassifyTarget(EntityClass::Station,  false, false, false, false), TargetKind::Station);
  EXPECT_EQ(ClassifyTarget(EntityClass::Planet,   false, false, false, false), TargetKind::Planet);
  EXPECT_EQ(ClassifyTarget(EntityClass::Sun,      false, false, false, false), TargetKind::Sun);
  EXPECT_EQ(ClassifyTarget(EntityClass::Canister, false, false, false, false), TargetKind::Canister);
}

TEST(OrderMenu, ShipClassificationDependsOnOwnershipAndWanted)
{
  // NPC hull -> enemy.
  EXPECT_EQ(ClassifyTarget(EntityClass::Ship, false, false, false, false), TargetKind::EnemyShip);
  // My own ship.
  EXPECT_EQ(ClassifyTarget(EntityClass::Ship, true, false, false, false), TargetKind::OwnShip);
  // Another unit I own (F1 escort).
  EXPECT_EQ(ClassifyTarget(EntityClass::Ship, false, true, false, false), TargetKind::OwnEscort);
  // A clean player.
  EXPECT_EQ(ClassifyTarget(EntityClass::Ship, false, false, true, false), TargetKind::CleanPlayer);
  // A wanted player -> attackable enemy.
  EXPECT_EQ(ClassifyTarget(EntityClass::Ship, false, false, true, true), TargetKind::EnemyShip);
}

TEST(OrderMenu, DefaultContextOrdersMatchTheDesignTable)
{
  EXPECT_EQ(DefaultContextOrder(TargetKind::Empty),       OrderKind::Move);
  EXPECT_EQ(DefaultContextOrder(TargetKind::OwnShip),     OrderKind::Stop);
  EXPECT_EQ(DefaultContextOrder(TargetKind::OwnEscort),   OrderKind::Escort);
  EXPECT_EQ(DefaultContextOrder(TargetKind::EnemyShip),   OrderKind::Attack);
  EXPECT_EQ(DefaultContextOrder(TargetKind::CleanPlayer), OrderKind::Approach);
  EXPECT_EQ(DefaultContextOrder(TargetKind::Station),     OrderKind::Dock);
  EXPECT_EQ(DefaultContextOrder(TargetKind::Planet),      OrderKind::Approach);
  EXPECT_EQ(DefaultContextOrder(TargetKind::Sun),         OrderKind::Approach);
  EXPECT_EQ(DefaultContextOrder(TargetKind::Canister),    OrderKind::Collect);
}

TEST(OrderMenu, CleanPlayerClickNeverAttacksButMenuOffersIt)
{
  EXPECT_TRUE(AttackRequiresMenu(TargetKind::CleanPlayer));
  EXPECT_NE(DefaultContextOrder(TargetKind::CleanPlayer), OrderKind::Attack);
  // The friction is: Attack is reachable, but only through the menu.
  const MenuOptions m = LegalOrders(TargetKind::CleanPlayer);
  EXPECT_TRUE(MenuHas(m, OrderKind::Attack));
  EXPECT_TRUE(MenuHas(m, OrderKind::Approach));
}

TEST(OrderMenu, EnemyClickAttacksWithoutFriction)
{
  EXPECT_FALSE(AttackRequiresMenu(TargetKind::EnemyShip));
  EXPECT_EQ(DefaultContextOrder(TargetKind::EnemyShip), OrderKind::Attack);
}

TEST(OrderMenu, RadialMenusListLegalOrdersPlusInfo)
{
  const MenuOptions station = LegalOrders(TargetKind::Station);
  EXPECT_TRUE(MenuHas(station, OrderKind::Dock));
  EXPECT_TRUE(MenuHas(station, OrderKind::Approach));
  EXPECT_TRUE(MenuHasInfo(station));

  const MenuOptions canister = LegalOrders(TargetKind::Canister);
  EXPECT_TRUE(MenuHas(canister, OrderKind::Collect));
  EXPECT_TRUE(MenuHasInfo(canister));
}

TEST(OrderMenu, EmptySpaceHasNoMenuJustMove)
{
  const MenuOptions empty = LegalOrders(TargetKind::Empty);
  EXPECT_TRUE(MenuHas(empty, OrderKind::Move));
  EXPECT_FALSE(MenuHasInfo(empty)); // gizmo, not a menu
}

TEST(OrderMenu, MenusNeverOverflowTheFixedBuffer)
{
  const TargetKind all[] = {
    TargetKind::Empty, TargetKind::OwnShip, TargetKind::OwnEscort,
    TargetKind::EnemyShip, TargetKind::CleanPlayer, TargetKind::Station,
    TargetKind::Planet, TargetKind::Sun, TargetKind::Canister,
  };
  for (TargetKind t : all)
    EXPECT_LE(LegalOrders(t).count, MAX_MENU_OPTIONS);
}
