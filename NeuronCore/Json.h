#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{
  // A small, dependency-free JSON value + parser. UTF-8 in, UTF-8 out.
  class JsonValue
  {
    public:
      enum class Type { Null, Boolean, Number, String, Array, Object };

      using ArrayType = std::vector<JsonValue>;
      using ObjectType = std::map<std::string, JsonValue>;

      JsonValue() = default;
      JsonValue(std::nullptr_t) {}
      explicit JsonValue(bool _value) : m_type(Type::Boolean), m_bool(_value) {}
      explicit JsonValue(double _value) : m_type(Type::Number), m_number(_value) {}
      explicit JsonValue(std::string _value) : m_type(Type::String), m_string(std::move(_value)) {}
      explicit JsonValue(ArrayType _value) : m_type(Type::Array), m_array(std::move(_value)) {}
      explicit JsonValue(ObjectType _value) : m_type(Type::Object), m_object(std::move(_value)) {}

      [[nodiscard]] Type GetType() const { return m_type; }
      [[nodiscard]] bool IsNull()   const { return m_type == Type::Null; }
      [[nodiscard]] bool IsBool()   const { return m_type == Type::Boolean; }
      [[nodiscard]] bool IsNumber() const { return m_type == Type::Number; }
      [[nodiscard]] bool IsString() const { return m_type == Type::String; }
      [[nodiscard]] bool IsArray()  const { return m_type == Type::Array; }
      [[nodiscard]] bool IsObject() const { return m_type == Type::Object; }

      [[nodiscard]] bool AsBool(bool _default = false) const { return IsBool() ? m_bool : _default; }
      [[nodiscard]] double AsNumber(double _default = 0.0) const { return IsNumber() ? m_number : _default; }
      [[nodiscard]] const std::string& AsString() const;
      [[nodiscard]] const ArrayType& AsArray() const;
      [[nodiscard]] const ObjectType& AsObject() const;

      // Object lookup; returns a Null value if the key is absent or this is not an object.
      [[nodiscard]] bool Contains(const std::string& _key) const;
      [[nodiscard]] const JsonValue& operator[](const std::string& _key) const;

      // Array lookup; returns a Null value if out of range or this is not an array.
      [[nodiscard]] const JsonValue& operator[](size_t _index) const;

      // Element count for arrays/objects (0 otherwise).
      [[nodiscard]] size_t Size() const;

    private:
      Type m_type = Type::Null;
      bool m_bool = false;
      double m_number = 0.0;
      std::string m_string;
      ArrayType m_array;
      ObjectType m_object;
  };

  class Json
  {
    public:
      // Parse UTF-8 JSON text. On success returns the parsed value and sets
      // *_ok to true; on failure returns a Null value and sets *_ok to false.
      [[nodiscard]] static JsonValue Parse(std::string_view _text, bool* _ok = nullptr);
  };
}
