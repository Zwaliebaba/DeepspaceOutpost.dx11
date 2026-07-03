#pragma once

// DatagramPump - a bounded per-tick datagram drain with magic routing (NeuronServer).
//
// Every Neuron server tick starts the same way: drain what the socket has (up to
// a per-tick budget, so a datagram flood cannot stall the simulation), peek each
// datagram's leading magic, and hand it to the protocol that owns that magic
// (snapshot stream, reliable lanes, message packets, ...). This is that loop,
// made generic: the caller supplies the routes; unknown magics are dropped on the
// floor, exactly as a UDP server must.
//
// The socket is a template parameter (anything with
// `int RecvFrom(uint8_t*, std::size_t, Net::Endpoint&)`), so the pump is
// unit-tested headlessly against a fake - no real socket, no OS networking.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "NetLib.h"        // Net::Endpoint
#include "Replication.h"   // Net::PeekMagic

namespace Neuron::Server
{
  // One protocol's claim on a leading magic: datagrams starting with `magic` go
  // to `handler` (endpoint + payload bytes).
  struct MagicRoute
  {
    uint32_t magic = 0;
    std::function<void(const Net::Endpoint&, const uint8_t*, std::size_t)> handler;
  };

  // Drain up to `_budget` datagrams from `_socket` into `_buf`, routing each by
  // its leading magic. Returns how many datagrams were handed to a route
  // (unroutable ones are received and dropped, but still count against the
  // budget - hostile garbage cannot buy extra socket reads).
  template <typename Socket>
  inline int PumpDatagrams(Socket& _socket, uint8_t* _buf, std::size_t _cap, int _budget,
                           const std::vector<MagicRoute>& _routes)
  {
    int handled = 0;
    Net::Endpoint from;
    for (int i = 0; i < _budget; ++i)
    {
      const int got = _socket.RecvFrom(_buf, _cap, from);
      if (got <= 0)
        break;

      const std::size_t size = static_cast<std::size_t>(got);
      const uint32_t magic = Net::PeekMagic(_buf, size);
      for (const MagicRoute& r : _routes)
        if (r.magic == magic)
        {
          if (r.handler)
          {
            r.handler(from, _buf, size);
            ++handled;
          }
          break;
        }
    }
    return handled;
  }
}
