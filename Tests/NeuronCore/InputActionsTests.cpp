#include <gtest/gtest.h>

#include "Messages/MessageBus.h"
#include "Messages/Serialize.h"
#include "Messages/Defs/InputActions.h"

using namespace Neuron;

TEST(InputActions, ActionTriggeredEncodesAndDecodes)
{
  Msg::ActionTriggered a{ Msg::InputAction::LaunchMissile, 4242 };
  Msg::ActionTriggered out;
  ASSERT_TRUE(Msg::Decode(Msg::Encode(a), out));
  EXPECT_TRUE(out.action == Msg::InputAction::LaunchMissile);
  EXPECT_EQ(out.param, 4242u);
}

// Mirror the client's command-builder: discrete actions on the bus set the frame's
// intent that the per-frame send folds into an InputCommand.
TEST(InputActions, CommandBuilderAccumulatesActions)
{
  Msg::MessageBus bus;
  bool fire = false;
  bool missile = false;
  uint32_t target = 0;
  bus.Subscribe<Msg::ActionTriggered>([&](const Msg::ActionTriggered& _a)
  {
    if (_a.action == Msg::InputAction::Fire)
      fire = true;
    else if (_a.action == Msg::InputAction::LaunchMissile)
    {
      missile = true;
      target = _a.param;
    }
  });

  bus.Publish(Msg::ActionTriggered{ Msg::InputAction::Fire, 0 });
  bus.Publish(Msg::ActionTriggered{ Msg::InputAction::LaunchMissile, 99 });
  bus.Dispatch();

  EXPECT_TRUE(fire);
  EXPECT_TRUE(missile);
  EXPECT_EQ(target, 99u);
}

TEST(InputActions, IsLocalOnlyAndNeverOnTheWire)
{
  EXPECT_TRUE(Msg::ActionTriggered::Scope == Msg::MessageScope::LocalOnly);
  EXPECT_TRUE(Msg::IsNonWireId(Msg::ActionTriggered::Id));
}
