#include "pch.h"

#include "NetLib.h"

// winsock2.h / ws2tcpip.h come in via pch.h -> NeuronCore.h.

namespace Neuron::Net
{
  namespace
  {
    int g_startupRefs = 0;
  }

  bool NetStartup()
  {
    if (g_startupRefs > 0)
    {
      ++g_startupRefs;
      return true;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return false;

    g_startupRefs = 1;
    return true;
  }

  void NetShutdown()
  {
    if (g_startupRefs <= 0)
      return;
    if (--g_startupRefs == 0)
      WSACleanup();
  }

  Endpoint MakeEndpoint(uint8_t _a, uint8_t _b, uint8_t _c, uint8_t _d, uint16_t _port)
  {
    Endpoint ep;
    ep.address = (static_cast<uint32_t>(_a) << 24) | (static_cast<uint32_t>(_b) << 16) |
                 (static_cast<uint32_t>(_c) << 8) | static_cast<uint32_t>(_d);
    ep.port = _port;
    return ep;
  }

  UdpSocket::~UdpSocket()
  {
    Close();
  }

  bool UdpSocket::Open(uint16_t _bindPort)
  {
    Close();

    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
      return false;

    if (_bindPort != 0)
    {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(_bindPort);
      if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
      {
        closesocket(s);
        return false;
      }
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR)
    {
      closesocket(s);
      return false;
    }

    m_handle = static_cast<uintptr_t>(s);
    return true;
  }

  void UdpSocket::Close()
  {
    if (m_handle != kInvalid)
    {
      closesocket(static_cast<SOCKET>(m_handle));
      m_handle = kInvalid;
    }
  }

  int UdpSocket::SendTo(const Endpoint& _to, const void* _data, std::size_t _size)
  {
    if (m_handle == kInvalid)
      return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(_to.address);
    addr.sin_port = htons(_to.port);

    const int sent = sendto(static_cast<SOCKET>(m_handle),
                            static_cast<const char*>(_data), static_cast<int>(_size), 0,
                            reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == SOCKET_ERROR ? -1 : sent;
  }

  int UdpSocket::RecvFrom(void* _buffer, std::size_t _capacity, Endpoint& _from)
  {
    if (m_handle == kInvalid)
      return -1;

    sockaddr_in addr{};
    int addrLen = sizeof(addr);
    const int got = recvfrom(static_cast<SOCKET>(m_handle),
                             static_cast<char*>(_buffer), static_cast<int>(_capacity), 0,
                             reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (got == SOCKET_ERROR)
    {
      return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
    }

    _from.address = ntohl(addr.sin_addr.s_addr);
    _from.port = ntohs(addr.sin_port);
    return got;
  }
}
