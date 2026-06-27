#pragma once

// DataReader - the exact inverse of DataWriter (NeuronCore).
//
// Reads fixed-width little-endian fields from a byte buffer in the same order
// DataWriter wrote them. Bounds-checked: a read past the end sets a sticky
// failure flag and returns zero rather than reading out of bounds, so a truncated
// or malformed packet from the network can never corrupt memory - the caller
// checks Ok() after decoding and drops the packet on failure.

#include <cstdint>
#include <cstring>
#include <cstddef>

namespace Neuron::Net
{
  class DataReader
  {
  public:
    DataReader(const uint8_t* _data, std::size_t _size)
      : m_data(_data), m_size(_size)
    {
    }

    [[nodiscard]] uint8_t ReadU8()
    {
      if (m_cursor + 1 > m_size)
      {
        m_failed = true;
        return 0;
      }
      return m_data[m_cursor++];
    }

    [[nodiscard]] uint16_t ReadU16()
    {
      const uint16_t lo = ReadU8();
      const uint16_t hi = ReadU8();
      return static_cast<uint16_t>(lo | (hi << 8));
    }

    [[nodiscard]] uint32_t ReadU32()
    {
      const uint32_t lo = ReadU16();
      const uint32_t hi = ReadU16();
      return lo | (hi << 16);
    }

    [[nodiscard]] uint64_t ReadU64()
    {
      const uint64_t lo = ReadU32();
      const uint64_t hi = ReadU32();
      return lo | (hi << 32);
    }

    [[nodiscard]] int32_t ReadI32() { return static_cast<int32_t>(ReadU32()); }
    [[nodiscard]] int64_t ReadI64() { return static_cast<int64_t>(ReadU64()); }

    [[nodiscard]] float ReadF32()
    {
      const uint32_t bits = ReadU32();
      float v;
      std::memcpy(&v, &bits, sizeof(v));
      return v;
    }

    [[nodiscard]] double ReadF64()
    {
      const uint64_t bits = ReadU64();
      double v;
      std::memcpy(&v, &bits, sizeof(v));
      return v;
    }

    // False once any read has run past the end of the buffer (sticky).
    [[nodiscard]] bool Ok() const { return !m_failed; }
    [[nodiscard]] std::size_t Remaining() const { return m_size - m_cursor; }

  private:
    const uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_cursor = 0;
    bool m_failed = false;
  };
}
