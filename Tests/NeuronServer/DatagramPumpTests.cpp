#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "DatagramPump.h"

using namespace Neuron;
using namespace Neuron::Server;

namespace
{
  // A scripted socket: hands out queued datagrams in order, then reports empty.
  // Matches the pump's duck-typed contract (int RecvFrom(buf, cap, from)).
  struct FakeSocket
  {
    std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>> queue;
    std::size_t next = 0;

    int RecvFrom(uint8_t* _buf, std::size_t _cap, Net::Endpoint& _from)
    {
      if (next >= queue.size())
        return 0;
      const auto& [ep, bytes] = queue[next++];
      const std::size_t n = bytes.size() < _cap ? bytes.size() : _cap;
      for (std::size_t i = 0; i < n; ++i)
        _buf[i] = bytes[i];
      _from = ep;
      return static_cast<int>(n);
    }
  };

  // A little-endian datagram starting with `magic` followed by one tag byte.
  std::vector<uint8_t> Datagram(uint32_t _magic, uint8_t _tag)
  {
    return {
      static_cast<uint8_t>(_magic), static_cast<uint8_t>(_magic >> 8),
      static_cast<uint8_t>(_magic >> 16), static_cast<uint8_t>(_magic >> 24),
      _tag,
    };
  }

  constexpr uint32_t MAGIC_A = 0x41414141u;
  constexpr uint32_t MAGIC_B = 0x42424242u;
}

TEST(DatagramPump, RoutesByLeadingMagic)
{
  FakeSocket sock;
  sock.queue.push_back({ Net::Endpoint{ 1, 10 }, Datagram(MAGIC_A, 7) });
  sock.queue.push_back({ Net::Endpoint{ 2, 20 }, Datagram(MAGIC_B, 9) });

  int aTags = 0, bTags = 0;
  Net::Endpoint aFrom{};
  const std::vector<MagicRoute> routes = {
    { MAGIC_A, [&](const Net::Endpoint& _f, const uint8_t* _d, std::size_t _n) { aFrom = _f; aTags = _d[4]; EXPECT_EQ(_n, 5u); } },
    { MAGIC_B, [&](const Net::Endpoint&, const uint8_t* _d, std::size_t) { bTags = _d[4]; } },
  };

  uint8_t buf[64];
  const int handled = PumpDatagrams(sock, buf, sizeof(buf), 16, routes);

  EXPECT_EQ(handled, 2);
  EXPECT_EQ(aTags, 7);
  EXPECT_EQ(bTags, 9);
  EXPECT_EQ(aFrom.address, 1u);   // the sender's endpoint reaches the handler
  EXPECT_EQ(aFrom.port, 10);
}

TEST(DatagramPump, UnknownMagicIsDroppedButStillConsumed)
{
  FakeSocket sock;
  sock.queue.push_back({ {}, Datagram(0xDEADBEEFu, 1) });   // nobody claims this
  sock.queue.push_back({ {}, Datagram(MAGIC_A, 2) });

  int aCalls = 0;
  const std::vector<MagicRoute> routes = {
    { MAGIC_A, [&](const Net::Endpoint&, const uint8_t*, std::size_t) { ++aCalls; } },
  };

  uint8_t buf[64];
  const int handled = PumpDatagrams(sock, buf, sizeof(buf), 16, routes);

  EXPECT_EQ(handled, 1);   // only the routed one counts as handled
  EXPECT_EQ(aCalls, 1);    // but the garbage datagram was read past, not stuck on
}

TEST(DatagramPump, TheBudgetBoundsSocketReads)
{
  FakeSocket sock;
  for (int i = 0; i < 10; ++i)
    sock.queue.push_back({ {}, Datagram(MAGIC_A, static_cast<uint8_t>(i)) });

  int calls = 0;
  const std::vector<MagicRoute> routes = {
    { MAGIC_A, [&](const Net::Endpoint&, const uint8_t*, std::size_t) { ++calls; } },
  };

  uint8_t buf[64];
  const int handled = PumpDatagrams(sock, buf, sizeof(buf), /*budget*/ 4, routes);

  EXPECT_EQ(handled, 4);           // a flood can't stall the tick...
  EXPECT_EQ(calls, 4);
  EXPECT_EQ(sock.next, 4u);        // ...and unread datagrams stay queued for next tick
}

TEST(DatagramPump, AnEmptySocketHandlesNothing)
{
  FakeSocket sock;
  int calls = 0;
  const std::vector<MagicRoute> routes = {
    { MAGIC_A, [&](const Net::Endpoint&, const uint8_t*, std::size_t) { ++calls; } },
  };
  uint8_t buf[64];
  EXPECT_EQ(PumpDatagrams(sock, buf, sizeof(buf), 16, routes), 0);
  EXPECT_EQ(calls, 0);
}
