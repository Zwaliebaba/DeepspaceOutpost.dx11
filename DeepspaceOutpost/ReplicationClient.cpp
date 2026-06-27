#include "pch.h"

#include "ReplicationClient.h"

namespace Neuron::Client
{
  namespace
  {
    // Largest datagram we expect (a single MTU-bounded snapshot packet, with a
    // little slack); anything bigger is a malformed packet and is discarded.
    constexpr int kRecvBufferSize = 2048;

    // Cap datagrams processed per Pump() so a flood can never stall the frame.
    constexpr int kMaxDrainPerPump = 256;
  }

  bool ReplicationClient::Open(uint16_t _port)
  {
    Close();
    if (!Net::NetStartup())
      return false;
    if (!m_socket.Open(_port))
    {
      Net::NetShutdown();
      return false;
    }
    m_open = true;
    return true;
  }

  void ReplicationClient::Close()
  {
    if (m_open)
    {
      m_socket.Close();
      Net::NetShutdown();
      m_open = false;
    }
  }

  void ReplicationClient::Pump()
  {
    if (!m_open)
      return;

    uint8_t buffer[kRecvBufferSize];
    Net::Endpoint from;

    for (int i = 0; i < kMaxDrainPerPump; ++i)
    {
      const int got = m_socket.RecvFrom(buffer, sizeof(buffer), from);
      if (got <= 0)
        break;   // 0 = nothing pending, <0 = error: stop draining this frame

      m_interp.Apply(buffer, static_cast<std::size_t>(got));
    }
  }

  ReplicationClient& ReplicationClientInstance()
  {
    static ReplicationClient instance;
    return instance;
  }
}
