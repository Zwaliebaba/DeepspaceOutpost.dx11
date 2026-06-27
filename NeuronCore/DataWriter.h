#pragma once

// DataWriter - hand-rolled little-endian binary serializer (NeuronCore).
//
// The MMO wire format is hand-rolled binary (not JSON, not a reflection lib) for
// the hot path: compact, explicit, and identical on both ends. DataWriter appends
// fixed-width fields to a growing byte buffer in explicit little-endian order, so
// the encoding is independent of host endianness and is byte-for-byte reproducible
// for golden tests. DataReader is its exact inverse.
//
// Shared data/protocol only - this is schema plumbing, never game behaviour, so it
// is safe for both client and server to link.

#include <cstdint>
#include <cstring>
#include <vector>

namespace Neuron::Net
{
  class DataWriter
  {
  public:
    void WriteU8(uint8_t _v) { m_buffer.push_back(_v); }

    void WriteU16(uint16_t _v)
    {
      WriteU8(static_cast<uint8_t>(_v & 0xFF));
      WriteU8(static_cast<uint8_t>((_v >> 8) & 0xFF));
    }

    void WriteU32(uint32_t _v)
    {
      WriteU16(static_cast<uint16_t>(_v & 0xFFFF));
      WriteU16(static_cast<uint16_t>((_v >> 16) & 0xFFFF));
    }

    void WriteU64(uint64_t _v)
    {
      WriteU32(static_cast<uint32_t>(_v & 0xFFFFFFFFu));
      WriteU32(static_cast<uint32_t>((_v >> 32) & 0xFFFFFFFFu));
    }

    void WriteI32(int32_t _v) { WriteU32(static_cast<uint32_t>(_v)); }
    void WriteI64(int64_t _v) { WriteU64(static_cast<uint64_t>(_v)); }

    void WriteF32(float _v)
    {
      uint32_t bits;
      std::memcpy(&bits, &_v, sizeof(bits));
      WriteU32(bits);
    }

    void WriteF64(double _v)
    {
      uint64_t bits;
      std::memcpy(&bits, &_v, sizeof(bits));
      WriteU64(bits);
    }

    [[nodiscard]] const std::vector<uint8_t>& Bytes() const { return m_buffer; }
    [[nodiscard]] const uint8_t* Data() const { return m_buffer.data(); }
    [[nodiscard]] std::size_t Size() const { return m_buffer.size(); }

    void Clear() { m_buffer.clear(); }

  private:
    std::vector<uint8_t> m_buffer;
  };
}
