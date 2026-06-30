#pragma once

// Serialize - generate a message's wire codec from its field list (NeuronCore).
//
// A message is a typed struct that describes its serialisable fields ONCE, via a
// `Fields()` member returning a tuple of references (std::tie). Encode/Decode fold
// over that tuple, dispatching each field to a leaf WriteField/ReadField overload.
// The fold inlines to exactly the DataWriter/DataReader calls one would write by
// hand (zero overhead), and a Fields()/member mismatch is a COMPILE error rather
// than silent wire drift. No per-message encode/decode is hand-written.
//
// Mechanism only (no behaviour). Reuses the existing little-endian, bounds-checked
// DataWriter/DataReader, so decode of a truncated/hostile buffer fails safely.
//
// Supported leaf field types: uint8/16/32/64, int32/64, float, double, bool, any
// enum (serialised as its underlying type), std::string (UTF-8, length-capped),
// NetEntityId, std::optional<T>, and std::vector<T> (count-capped). Add a leaf
// pair to extend; a field of an unsupported type is a compile error.

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"

#include "MessageId.h"
#include "MessageTraits.h"
#include "NetEntityId.h"

namespace Neuron::Msg
{
  using Neuron::Net::DataReader;
  using Neuron::Net::DataWriter;

  // Hard caps so a hostile length can never drive an unbounded allocation.
  inline constexpr uint16_t MAX_STRING_LEN = 4096;
  inline constexpr uint16_t MAX_VECTOR_ELEMENTS = 4096;

  // A message: a stable id, the full set of policy traits, and a self-description
  // of its fields (both const and non-const, for Encode and Decode respectively).
  template <typename T>
  concept Message = requires (T& _t, const T& _ct) {
    { T::Id }    -> std::convertible_to<MessageId>;
    { T::Scope } -> std::convertible_to<MessageScope>;
    { T::Kind }  -> std::convertible_to<MessageKind>;
    { T::Lane }  -> std::convertible_to<MessageLane>;
    { T::Dir }   -> std::convertible_to<Direction>;
    _t.Fields();
    _ct.Fields();
  };

  // --- Leaf writers ---------------------------------------------------------

  inline void WriteField(DataWriter& _w, uint8_t _v)  { _w.WriteU8(_v); }
  inline void WriteField(DataWriter& _w, uint16_t _v) { _w.WriteU16(_v); }
  inline void WriteField(DataWriter& _w, uint32_t _v) { _w.WriteU32(_v); }
  inline void WriteField(DataWriter& _w, uint64_t _v) { _w.WriteU64(_v); }
  inline void WriteField(DataWriter& _w, int32_t _v)  { _w.WriteI32(_v); }
  inline void WriteField(DataWriter& _w, int64_t _v)  { _w.WriteI64(_v); }
  inline void WriteField(DataWriter& _w, float _v)    { _w.WriteF32(_v); }
  inline void WriteField(DataWriter& _w, double _v)   { _w.WriteF64(_v); }
  inline void WriteField(DataWriter& _w, bool _v)     { _w.WriteU8(_v ? 1 : 0); }

  inline void WriteField(DataWriter& _w, const NetEntityId& _v)
  {
    _w.WriteU32(_v.index);
    _w.WriteU32(_v.generation);
  }

  // Enums travel as their explicit underlying type.
  template <typename E>
    requires std::is_enum_v<E>
  void WriteField(DataWriter& _w, E _v)
  {
    WriteField(_w, static_cast<std::underlying_type_t<E>>(_v));
  }

  inline void WriteField(DataWriter& _w, const std::string& _s)
  {
    const uint16_t n = static_cast<uint16_t>(std::min<std::size_t>(_s.size(), MAX_STRING_LEN));
    _w.WriteU16(n);
    for (uint16_t i = 0; i < n; ++i)
      _w.WriteU8(static_cast<uint8_t>(_s[i]));
  }

  template <typename T>
  void WriteField(DataWriter& _w, const std::optional<T>& _o)
  {
    _w.WriteU8(_o.has_value() ? 1 : 0);
    if (_o.has_value())
      WriteField(_w, *_o);
  }

  template <typename T>
  void WriteField(DataWriter& _w, const std::vector<T>& _v)
  {
    const uint16_t n = static_cast<uint16_t>(std::min<std::size_t>(_v.size(), MAX_VECTOR_ELEMENTS));
    _w.WriteU16(n);
    for (uint16_t i = 0; i < n; ++i)
      WriteField(_w, _v[i]);
  }

  // --- Leaf readers (return false on truncation or a cap violation) ----------

  [[nodiscard]] inline bool ReadField(DataReader& _r, uint8_t& _v)  { _v = _r.ReadU8();  return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, uint16_t& _v) { _v = _r.ReadU16(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, uint32_t& _v) { _v = _r.ReadU32(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, uint64_t& _v) { _v = _r.ReadU64(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, int32_t& _v)  { _v = _r.ReadI32(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, int64_t& _v)  { _v = _r.ReadI64(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, float& _v)    { _v = _r.ReadF32(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, double& _v)   { _v = _r.ReadF64(); return _r.Ok(); }
  [[nodiscard]] inline bool ReadField(DataReader& _r, bool& _v)     { _v = (_r.ReadU8() != 0); return _r.Ok(); }

  [[nodiscard]] inline bool ReadField(DataReader& _r, NetEntityId& _v)
  {
    _v.index = _r.ReadU32();
    _v.generation = _r.ReadU32();
    return _r.Ok();
  }

  template <typename E>
    requires std::is_enum_v<E>
  [[nodiscard]] bool ReadField(DataReader& _r, E& _v)
  {
    std::underlying_type_t<E> u{};
    if (!ReadField(_r, u))
      return false;
    _v = static_cast<E>(u);
    return true;
  }

  [[nodiscard]] inline bool ReadField(DataReader& _r, std::string& _s)
  {
    const uint16_t n = _r.ReadU16();
    if (!_r.Ok())
      return false;
    if (n > MAX_STRING_LEN)
      return false;                 // cap enforced before any sized allocation
    _s.clear();
    _s.reserve(n);
    for (uint16_t i = 0; i < n; ++i)
    {
      const uint8_t c = _r.ReadU8();
      if (!_r.Ok())
        return false;
      _s.push_back(static_cast<char>(c));
    }
    return true;
  }

  template <typename T>
  [[nodiscard]] bool ReadField(DataReader& _r, std::optional<T>& _o)
  {
    const uint8_t has = _r.ReadU8();
    if (!_r.Ok())
      return false;
    if (has == 0)
    {
      _o.reset();
      return true;
    }
    T v{};
    if (!ReadField(_r, v))
      return false;
    _o = std::move(v);
    return true;
  }

  template <typename T>
  [[nodiscard]] bool ReadField(DataReader& _r, std::vector<T>& _v)
  {
    const uint16_t n = _r.ReadU16();
    if (!_r.Ok())
      return false;
    if (n > MAX_VECTOR_ELEMENTS)
      return false;                 // cap enforced before any sized allocation
    _v.clear();
    _v.reserve(n);
    for (uint16_t i = 0; i < n; ++i)
    {
      T e{};
      if (!ReadField(_r, e))
        return false;
      _v.push_back(std::move(e));
    }
    return true;
  }

  // --- Whole-message encode / decode ----------------------------------------

  template <Message M>
  [[nodiscard]] std::vector<uint8_t> Encode(const M& _m)
  {
    DataWriter w;
    std::apply([&](const auto&... _fs) { (WriteField(w, _fs), ...); }, _m.Fields());
    return w.Bytes();
  }

  template <Message M>
  [[nodiscard]] bool Decode(DataReader& _r, M& _out)
  {
    bool ok = true;
    std::apply([&](auto&... _fs) { ((ok = ok && ReadField(_r, _fs)), ...); }, _out.Fields());
    return ok && _r.Ok();
  }

  template <Message M>
  [[nodiscard]] bool Decode(const std::vector<uint8_t>& _bytes, M& _out)
  {
    DataReader r(_bytes.data(), _bytes.size());
    return Decode(r, _out);
  }
}
