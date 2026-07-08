#include <gtest/gtest.h>

#include <vector>

#include "Messages/MessageBus.h"
#include "Messages/Serialize.h"
#include "Messages/Defs/InputActions.h"
#include "Messages/Defs/UnitOrder.h"   // Msg::AbilityRequest / AbilityKind

using namespace Neuron;

TEST(InputActions, ActionTriggeredEncodesAndDecodes)
{
  Msg::ActionTriggered a{ Msg::InputAction::LaunchMissile, 4242 };
  Msg::ActionTriggered out;
  ASSERT_TRUE(Msg::Decode(Msg::Encode(a), out));
  EXPECT_TRUE(out.action == Msg::InputAction::LaunchMissile);
  EXPECT_EQ(out.param, 4242u);
}

// Mirror the client's ability dispatch: a discrete action on the bus maps onto
// the reliable AbilityRequest the subscriber sends. The Fire action maps to
// nothing - the manual laser is retired (attack is an order), so it must produce
// no request.
TEST(InputActions, AbilityDispatchMapsActionsOntoAbilityRequests)
{
  Msg::MessageBus bus;
  std::vector<Msg::AbilityRequest> sent;
  bus.Subscribe<Msg::ActionTriggered>([&](const Msg::ActionTriggered& _a)
  {
    Msg::AbilityRequest req;
    switch (_a.action)
    {
      case Msg::InputAction::LaunchMissile:
        req.kind = Msg::AbilityKind::FireMissile;
        req.target = _a.param;
        break;
      case Msg::InputAction::Ecm:        req.kind = Msg::AbilityKind::Ecm;        break;
      case Msg::InputAction::EnergyBomb: req.kind = Msg::AbilityKind::EnergyBomb; break;
      case Msg::InputAction::EscapePod:  req.kind = Msg::AbilityKind::EscapePod;  break;
      default:
        return;   // Fire: retired, never a request
    }
    sent.push_back(req);
  });

  bus.Publish(Msg::ActionTriggered{ Msg::InputAction::Fire, 0 });            // retired
  bus.Publish(Msg::ActionTriggered{ Msg::InputAction::LaunchMissile, 99 });
  bus.Publish(Msg::ActionTriggered{ Msg::InputAction::Ecm, 0 });
  bus.Dispatch();

  ASSERT_EQ(sent.size(), 2u);
  EXPECT_TRUE(sent[0].kind == Msg::AbilityKind::FireMissile);
  EXPECT_EQ(sent[0].target, 99u);
  EXPECT_TRUE(sent[1].kind == Msg::AbilityKind::Ecm);
}

TEST(InputActions, IsLocalOnlyAndNeverOnTheWire)
{
  EXPECT_TRUE(Msg::ActionTriggered::Scope == Msg::MessageScope::LocalOnly);
  EXPECT_TRUE(Msg::IsNonWireId(Msg::ActionTriggered::Id));
}
