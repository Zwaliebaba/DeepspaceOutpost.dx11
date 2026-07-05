#include <gtest/gtest.h>

#include <string>

#include "ChatModeration.h"

using namespace Neuron::GameLogic;

TEST(ChatModeration, AllowsUpToTheCapThenBlocks)
{
  ChatLimiter lim;
  for (int i = 0; i < CHAT_MAX_PER_WINDOW; ++i)
    EXPECT_TRUE(ChatAllowed(lim, /*tick*/ 100)) << "line " << i;
  EXPECT_FALSE(ChatAllowed(lim, 100));   // one past the cap in the same window
  EXPECT_FALSE(ChatAllowed(lim, 100 + CHAT_WINDOW_TICKS - 1));  // still inside the window
}

TEST(ChatModeration, ResetsAfterTheWindow)
{
  ChatLimiter lim;
  for (int i = 0; i < CHAT_MAX_PER_WINDOW; ++i)
    ChatAllowed(lim, 100);
  EXPECT_FALSE(ChatAllowed(lim, 100));
  // A tick a full window later re-opens the allowance.
  EXPECT_TRUE(ChatAllowed(lim, 100 + CHAT_WINDOW_TICKS));
}

TEST(ChatModeration, StripsControlBytesAndTrims)
{
  const std::string dirty = std::string("hi\x01\x02 there\n\t  ");
  const std::string clean = SanitizeChat(dirty);
  EXPECT_EQ(clean, "hi there");   // controls (incl. \n, \t) dropped, trailing spaces trimmed
}

TEST(ChatModeration, KeepsUtf8AndPrintableAscii)
{
  const std::string s = "Caf\xC3\xA9 42!";   // "Café 42!" in UTF-8
  EXPECT_EQ(SanitizeChat(s), s);
}

TEST(ChatModeration, CapsOverlongLines)
{
  const std::string huge(CHAT_MAX_LEN + 50, 'x');
  EXPECT_EQ(SanitizeChat(huge).size(), CHAT_MAX_LEN);
}

TEST(ChatModeration, EmptyOrAllControlBecomesEmpty)
{
  EXPECT_TRUE(SanitizeChat("").empty());
  EXPECT_TRUE(SanitizeChat(std::string("\x01\x02\x03")).empty());
  EXPECT_TRUE(SanitizeChat("     ").empty());   // only spaces -> trimmed away
}
