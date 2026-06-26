#include "pch.h"
#include "Json.h"

#include <charconv>
#include <cstdint>

namespace Neuron
{
  namespace
  {
    const JsonValue g_null;

    // Recursive-descent parser over a UTF-8 buffer.
    class Parser
    {
      public:
        explicit Parser(std::string_view _text) : m_text(_text) {}

        bool Parse(JsonValue& _out)
        {
          SkipByteOrderMark();
          SkipWhitespace();
          if (!ParseValue(_out))
            return false;
          SkipWhitespace();
          return m_pos == m_text.size();
        }

      private:
        std::string_view m_text;
        size_t m_pos = 0;

        [[nodiscard]] bool AtEnd() const { return m_pos >= m_text.size(); }
        [[nodiscard]] char Peek() const { return m_text[m_pos]; }

        void SkipByteOrderMark()
        {
          if (m_text.size() >= 3 &&
              static_cast<unsigned char>(m_text[0]) == 0xEF &&
              static_cast<unsigned char>(m_text[1]) == 0xBB &&
              static_cast<unsigned char>(m_text[2]) == 0xBF)
            m_pos = 3;
        }

        void SkipWhitespace()
        {
          while (!AtEnd())
          {
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
              ++m_pos;
            else
              break;
          }
        }

        bool ParseValue(JsonValue& _out)
        {
          if (AtEnd())
            return false;

          switch (Peek())
          {
            case '{': return ParseObject(_out);
            case '[': return ParseArray(_out);
            case '"': return ParseString(_out);
            case 't':
            case 'f': return ParseBool(_out);
            case 'n': return ParseNull(_out);
            default:  return ParseNumber(_out);
          }
        }

        bool ParseLiteral(std::string_view _literal)
        {
          if (m_text.compare(m_pos, _literal.size(), _literal) != 0)
            return false;
          m_pos += _literal.size();
          return true;
        }

        bool ParseNull(JsonValue& _out)
        {
          if (!ParseLiteral("null"))
            return false;
          _out = JsonValue();
          return true;
        }

        bool ParseBool(JsonValue& _out)
        {
          if (ParseLiteral("true")) { _out = JsonValue(true); return true; }
          if (ParseLiteral("false")) { _out = JsonValue(false); return true; }
          return false;
        }

        bool ParseNumber(JsonValue& _out)
        {
          const size_t start = m_pos;
          while (!AtEnd())
          {
            const char c = Peek();
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' ||
                c == '.' || c == 'e' || c == 'E')
              ++m_pos;
            else
              break;
          }

          if (m_pos == start)
            return false;

          double value = 0.0;
          const char* first = m_text.data() + start;
          const char* last = m_text.data() + m_pos;
          const auto [ptr, ec] = std::from_chars(first, last, value);
          if (ec != std::errc() || ptr != last)
            return false;

          _out = JsonValue(value);
          return true;
        }

        // Parses a JSON string (assumes the leading quote is present).
        bool ParseRawString(std::string& _out)
        {
          if (AtEnd() || Peek() != '"')
            return false;
          ++m_pos; // opening quote

          _out.clear();
          while (!AtEnd())
          {
            const char c = m_text[m_pos++];
            if (c == '"')
              return true;

            if (c != '\\')
            {
              _out.push_back(c);
              continue;
            }

            if (AtEnd())
              return false;

            const char esc = m_text[m_pos++];
            switch (esc)
            {
              case '"':  _out.push_back('"');  break;
              case '\\': _out.push_back('\\'); break;
              case '/':  _out.push_back('/');  break;
              case 'b':  _out.push_back('\b'); break;
              case 'f':  _out.push_back('\f'); break;
              case 'n':  _out.push_back('\n'); break;
              case 'r':  _out.push_back('\r'); break;
              case 't':  _out.push_back('\t'); break;
              case 'u':  if (!ParseUnicodeEscape(_out)) return false; break;
              default:   return false;
            }
          }

          return false; // unterminated string
        }

        bool ParseString(JsonValue& _out)
        {
          std::string value;
          if (!ParseRawString(value))
            return false;
          _out = JsonValue(std::move(value));
          return true;
        }

        bool ParseHex4(uint32_t& _out)
        {
          if (m_pos + 4 > m_text.size())
            return false;

          uint32_t value = 0;
          for (int i = 0; i < 4; ++i)
          {
            const char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9')      value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
          }
          _out = value;
          return true;
        }

        // Handles a \uXXXX escape (the 'u' has already been consumed),
        // including surrogate pairs, and appends the UTF-8 encoding.
        bool ParseUnicodeEscape(std::string& _out)
        {
          uint32_t code = 0;
          if (!ParseHex4(code))
            return false;

          // High surrogate: expect a following \uXXXX low surrogate.
          if (code >= 0xD800 && code <= 0xDBFF)
          {
            if (m_pos + 2 > m_text.size() || m_text[m_pos] != '\\' || m_text[m_pos + 1] != 'u')
              return false;
            m_pos += 2;

            uint32_t low = 0;
            if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
              return false;

            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }

          AppendUtf8(code, _out);
          return true;
        }

        static void AppendUtf8(uint32_t _code, std::string& _out)
        {
          if (_code <= 0x7F)
          {
            _out.push_back(static_cast<char>(_code));
          }
          else if (_code <= 0x7FF)
          {
            _out.push_back(static_cast<char>(0xC0 | (_code >> 6)));
            _out.push_back(static_cast<char>(0x80 | (_code & 0x3F)));
          }
          else if (_code <= 0xFFFF)
          {
            _out.push_back(static_cast<char>(0xE0 | (_code >> 12)));
            _out.push_back(static_cast<char>(0x80 | ((_code >> 6) & 0x3F)));
            _out.push_back(static_cast<char>(0x80 | (_code & 0x3F)));
          }
          else
          {
            _out.push_back(static_cast<char>(0xF0 | (_code >> 18)));
            _out.push_back(static_cast<char>(0x80 | ((_code >> 12) & 0x3F)));
            _out.push_back(static_cast<char>(0x80 | ((_code >> 6) & 0x3F)));
            _out.push_back(static_cast<char>(0x80 | (_code & 0x3F)));
          }
        }

        bool ParseArray(JsonValue& _out)
        {
          ++m_pos; // consume '['
          JsonValue::ArrayType elements;

          SkipWhitespace();
          if (!AtEnd() && Peek() == ']') { ++m_pos; _out = JsonValue(std::move(elements)); return true; }

          while (true)
          {
            SkipWhitespace();
            JsonValue element;
            if (!ParseValue(element))
              return false;
            elements.push_back(std::move(element));

            SkipWhitespace();
            if (AtEnd())
              return false;
            const char c = m_text[m_pos++];
            if (c == ']') break;
            if (c != ',') return false;
          }

          _out = JsonValue(std::move(elements));
          return true;
        }

        bool ParseObject(JsonValue& _out)
        {
          ++m_pos; // consume '{'
          JsonValue::ObjectType members;

          SkipWhitespace();
          if (!AtEnd() && Peek() == '}') { ++m_pos; _out = JsonValue(std::move(members)); return true; }

          while (true)
          {
            SkipWhitespace();
            std::string key;
            if (!ParseRawString(key))
              return false;

            SkipWhitespace();
            if (AtEnd() || m_text[m_pos++] != ':')
              return false;

            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value))
              return false;
            members.insert_or_assign(std::move(key), std::move(value));

            SkipWhitespace();
            if (AtEnd())
              return false;
            const char c = m_text[m_pos++];
            if (c == '}') break;
            if (c != ',') return false;
          }

          _out = JsonValue(std::move(members));
          return true;
        }
    };
  }

  const std::string& JsonValue::AsString() const
  {
    static const std::string empty;
    return IsString() ? m_string : empty;
  }

  const JsonValue::ArrayType& JsonValue::AsArray() const
  {
    static const ArrayType empty;
    return IsArray() ? m_array : empty;
  }

  const JsonValue::ObjectType& JsonValue::AsObject() const
  {
    static const ObjectType empty;
    return IsObject() ? m_object : empty;
  }

  bool JsonValue::Contains(const std::string& _key) const
  {
    return IsObject() && m_object.contains(_key);
  }

  const JsonValue& JsonValue::operator[](const std::string& _key) const
  {
    if (IsObject())
    {
      if (const auto it = m_object.find(_key); it != m_object.end())
        return it->second;
    }
    return g_null;
  }

  const JsonValue& JsonValue::operator[](size_t _index) const
  {
    if (IsArray() && _index < m_array.size())
      return m_array[_index];
    return g_null;
  }

  size_t JsonValue::Size() const
  {
    if (IsArray())  return m_array.size();
    if (IsObject()) return m_object.size();
    return 0;
  }

  JsonValue Json::Parse(std::string_view _text, bool* _ok)
  {
    JsonValue result;
    Parser parser(_text);
    const bool ok = parser.Parse(result);
    if (_ok)
      *_ok = ok;
    return ok ? result : JsonValue();
  }
}
