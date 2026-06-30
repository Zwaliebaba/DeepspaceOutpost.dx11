#include <gtest/gtest.h>

#include <string>

#include "Messages/CatalogTools.h"
#include "Messages/Catalog.h"        // populates GlobalRegistry()
#include "MessageTestMessages.h"     // local-registry fixtures

using namespace Neuron;
using namespace MsgTest;

TEST(CatalogTools, ExportListsEntriesSortedById)
{
  Msg::MessageRegistry reg;
  reg.Add<Pong>("Pong");         // 0x0102
  reg.Add<Kitchen>("Kitchen");   // 0x0101

  const std::string text = Msg::ExportCatalogText(reg);
  const std::string::size_type kpos = text.find("Kitchen");
  const std::string::size_type ppos = text.find("Pong");
  ASSERT_NE(kpos, std::string::npos);
  ASSERT_NE(ppos, std::string::npos);
  EXPECT_LT(kpos, ppos);                              // sorted by id
  EXPECT_NE(text.find("0x0101"), std::string::npos);
  EXPECT_NE(text.find("scope=Wire"), std::string::npos);
  EXPECT_NE(text.find("dir=C->S"), std::string::npos);
}

TEST(CatalogTools, DiffDetectsAddedRemovedChanged)
{
  Msg::MessageRegistry oldReg;
  oldReg.Add<Kitchen>("Kitchen");   // 0x0101
  oldReg.Add<Pong>("Pong");         // 0x0102
  oldReg.Add<TickEv>("TickEv");     // 0x8001

  Msg::MessageRegistry newReg;
  newReg.Add<Kitchen>("Kitchen");        // unchanged
  newReg.Add<Pong>("PongRenamed");       // same id, new name -> changed
  newReg.Add<CtrlMsg>("CtrlMsg");        // 0x0002 -> added

  const Msg::CatalogDiff d = Msg::DiffCatalogs(oldReg, newReg);
  ASSERT_EQ(d.added.size(), 1u);
  EXPECT_TRUE(d.added[0] == CtrlMsg::Id);
  ASSERT_EQ(d.removed.size(), 1u);
  EXPECT_TRUE(d.removed[0] == TickEv::Id);
  ASSERT_EQ(d.changed.size(), 1u);
  EXPECT_TRUE(d.changed[0] == Pong::Id);
  EXPECT_FALSE(d.Empty());
  EXPECT_NE(Msg::FormatDiff(d).find("0x0002"), std::string::npos);
}

TEST(CatalogTools, IdenticalCatalogsDiffEmpty)
{
  Msg::MessageRegistry a;
  a.Add<Kitchen>("Kitchen");
  Msg::MessageRegistry b;
  b.Add<Kitchen>("Kitchen");
  EXPECT_TRUE(Msg::DiffCatalogs(a, b).Empty());
}

TEST(CatalogTools, GlobalCatalogExportsTheRealMessages)
{
  const std::string text = Msg::ExportCatalogText(Msg::GlobalRegistry());
  EXPECT_NE(text.find("InputCommand"), std::string::npos);
  EXPECT_NE(text.find("AssignPlayer"), std::string::npos);
  EXPECT_NE(text.find("EntityDeath"), std::string::npos);
  EXPECT_NE(text.find("StationRequest"), std::string::npos);
  EXPECT_NE(text.find("ActionTriggered"), std::string::npos);
}
