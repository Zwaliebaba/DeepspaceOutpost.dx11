#pragma once

// NetLib - raw UDP datagram transport (NeuronCore).
//
// The MMO transport is raw winsock UDP with a custom reliability layer on top.
// This is the bottom layer: a thin, non-blocking UDP socket. Sequencing, acks and
// resends are layered above it later; here we just move datagrams. The winsock
// SOCKET is kept opaque (a uintptr_t) so this header pulls in no Windows headers
// and can be included anywhere.

#include <cstdint>
#include <cstddef>

namespace Neuron::Net
{
  // Initialise / tear down the OS networking stack (WSAStartup/WSACleanup),
  // reference-counted so nested callers are safe. Call once before opening any
  // socket; the matching Shutdown when done.
  bool NetStartup();
  void NetShutdown();

  // An IPv4 destination: address in host byte order (127.0.0.1 == 0x7F000001)
  // plus a port.
  struct Endpoint
  {
    uint32_t address = 0;
    uint16_t port = 0;

    [[nodiscard]] friend bool operator==(const Endpoint&, const Endpoint&) = default;
  };

  // Build an Endpoint from dotted-quad octets and a port.
  [[nodiscard]] Endpoint MakeEndpoint(uint8_t _a, uint8_t _b, uint8_t _c, uint8_t _d, uint16_t _port);

  // A non-blocking UDP socket.
  class UdpSocket
  {
  public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Open a UDP socket. When `_bindPort` is non-zero the socket is bound to it
    // (the server listens there); zero leaves it unbound (a sender). The socket
    // is set non-blocking. Returns false on failure.
    bool Open(uint16_t _bindPort = 0);
    void Close();
    [[nodiscard]] bool IsOpen() const { return m_handle != kInvalid; }

    // Send one datagram to `_to`. Returns bytes sent, or -1 on error.
    int SendTo(const Endpoint& _to, const void* _data, std::size_t _size);

    // Receive one pending datagram into `_buffer` (capacity `_capacity`). Returns
    // the byte count, 0 when nothing is pending (would-block), or -1 on error.
    // `_from` is filled with the sender when a datagram is returned.
    int RecvFrom(void* _buffer, std::size_t _capacity, Endpoint& _from);

  private:
    static constexpr uintptr_t kInvalid = ~static_cast<uintptr_t>(0);   // INVALID_SOCKET
    uintptr_t m_handle = kInvalid;
  };
}
