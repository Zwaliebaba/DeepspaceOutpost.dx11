#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "Json.h"

using namespace Neuron;

// NeuronCore's small dependency-free JSON parser (Json.h/.cpp). It is a cold-path
// helper (config/tools), but a hand-rolled recursive-descent parser over untrusted
// UTF-8 is exactly the kind of code that must reject malformed input cleanly - so
// alongside the round-trip cases these tests fuzz it with garbage and truncated
// input, mirroring Tests/NeuronCore/MessageFuzzTests.cpp.

// ---- scalars ---------------------------------------------------------------

TEST(Json, ParsesNullBoolAndNumber)
{
  bool ok = false;

  JsonValue n = Json::Parse("null", &ok);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(n.IsNull());

  JsonValue t = Json::Parse("true", &ok);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(t.IsBool());
  EXPECT_TRUE(t.AsBool());

  JsonValue f = Json::Parse("false", &ok);
  EXPECT_TRUE(ok);
  EXPECT_FALSE(f.AsBool(true));

  struct Num { std::string_view text; double value; };
  const Num nums[] = { { "42", 42.0 }, { "-3.5", -3.5 }, { "0", 0.0 }, { "1.5e3", 1500.0 } };
  for (const Num& c : nums)
  {
    JsonValue num = Json::Parse(c.text, &ok);
    EXPECT_TRUE(ok) << c.text;
    EXPECT_TRUE(num.IsNumber()) << c.text;
    EXPECT_EQ(num.AsNumber(), c.value) << c.text;
  }
}

// ---- strings & escapes -----------------------------------------------------

TEST(Json, ParsesStringEscapes)
{
  bool ok = false;
  JsonValue s = Json::Parse(R"("tab\tnewline\nquote\"slash\/back\\")", &ok);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(s.IsString());
  EXPECT_EQ(s.AsString(), std::string("tab\tnewline\nquote\"slash/back\\"));
}

TEST(Json, DecodesUnicodeEscapes)
{
  // \uXXXX escapes decode to their UTF-8 encoding. The JSON source is kept
  // ASCII-only (escape sequences, not literal bytes) so it does not depend on the
  // compiler's source encoding; the expected values are the raw UTF-8 bytes.
  bool ok = false;

  EXPECT_EQ(Json::Parse("\"\\u0041\"", &ok).AsString(), std::string("A"));          // U+0041, 1-byte
  EXPECT_TRUE(ok);
  EXPECT_EQ(Json::Parse("\"\\u00e9\"", &ok).AsString(), std::string("\xC3\xA9"));   // U+00E9 e-acute, 2-byte
  EXPECT_TRUE(ok);
  EXPECT_EQ(Json::Parse("\"\\u20ac\"", &ok).AsString(), std::string("\xE2\x82\xAC")); // U+20AC euro, 3-byte
  EXPECT_TRUE(ok);

  // A surrogate pair decodes to one astral code point (U+1F600) as 4-byte UTF-8.
  EXPECT_EQ(Json::Parse("\"\\ud83d\\ude00\"", &ok).AsString(), std::string("\xF0\x9F\x98\x80"));
  EXPECT_TRUE(ok);
}

TEST(Json, RejectsBrokenSurrogatePair)
{
  bool ok = true;
  // A high surrogate with no following low surrogate is invalid.
  (void)Json::Parse("\"\\ud83d\"", &ok);
  EXPECT_FALSE(ok);

  ok = true;
  (void)Json::Parse("\"\\ud83dxx\"", &ok);
  EXPECT_FALSE(ok);
}

// ---- arrays & objects ------------------------------------------------------

TEST(Json, ParsesArrays)
{
  bool ok = false;
  JsonValue a = Json::Parse("[1, 2, 3]", &ok);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(a.IsArray());
  EXPECT_EQ(a.Size(), 3u);
  EXPECT_EQ(a[0].AsNumber(), 1.0);
  EXPECT_EQ(a[2].AsNumber(), 3.0);

  EXPECT_EQ(Json::Parse("[]", &ok).Size(), 0u);
  EXPECT_TRUE(ok);
}

TEST(Json, ParsesObjectsAndNesting)
{
  bool ok = false;
  JsonValue o = Json::Parse(R"({ "name": "outpost", "hp": 100, "online": true, "tags": ["a", "b"] })", &ok);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(o.IsObject());
  EXPECT_EQ(o.Size(), 4u);
  EXPECT_TRUE(o.Contains("name"));
  EXPECT_EQ(o["name"].AsString(), std::string("outpost"));
  EXPECT_EQ(o["hp"].AsNumber(), 100.0);
  EXPECT_TRUE(o["online"].AsBool());
  ASSERT_TRUE(o["tags"].IsArray());
  EXPECT_EQ(o["tags"][1].AsString(), std::string("b"));
}

TEST(Json, DuplicateKeyKeepsLastValue)
{
  bool ok = false;
  JsonValue o = Json::Parse(R"({"a":1,"a":2})", &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(o.Size(), 1u);
  EXPECT_EQ(o["a"].AsNumber(), 2.0);   // insert_or_assign: last wins
}

TEST(Json, ToleratesWhitespaceAndByteOrderMark)
{
  bool ok = false;
  EXPECT_TRUE(Json::Parse("  \n\t [ 1 ,\r\n 2 ] \n", &ok).IsArray());
  EXPECT_TRUE(ok);

  // A leading UTF-8 BOM is skipped.
  JsonValue v = Json::Parse(std::string_view("\xEF\xBB\xBF" "true", 7), &ok);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(v.AsBool());
}

// ---- accessor safety -------------------------------------------------------

TEST(Json, WrongTypeAccessorsReturnDefaults)
{
  bool ok = false;
  JsonValue arr = Json::Parse("[10]", &ok);
  ASSERT_TRUE(ok);

  // Type-mismatched reads fall back to the supplied/default value, never throw.
  EXPECT_EQ(arr.AsNumber(-1.0), -1.0);          // an array is not a number
  EXPECT_TRUE(arr.AsString().empty());          // ...nor a string
  EXPECT_FALSE(arr.AsBool());

  // Out-of-range and wrong-shape lookups yield a Null value.
  EXPECT_TRUE(arr[5].IsNull());                 // past the end
  EXPECT_TRUE(arr["nope"].IsNull());            // object lookup on an array
  EXPECT_FALSE(arr.Contains("nope"));
}

// ---- malformed input is rejected, never crashes ----------------------------

TEST(Json, RejectsMalformedInput)
{
  const char* bad[] = {
    "",                 // empty
    "   ",              // whitespace only
    "1 2",              // trailing token
    "truefoo",          // trailing garbage after a literal
    "nul",              // incomplete keyword
    "+5",               // JSON numbers may not lead with '+'
    "abc",              // bare word
    "[1, 2, ]",         // trailing comma (array)
    "{\"a\":1,}",       // trailing comma (object)
    "[1, 2",            // unterminated array
    "{\"a\":1",         // unterminated object
    "\"unterminated",   // unterminated string
    "{\"a\" 1}",        // missing colon
    "{1:2}",            // non-string key
    "\"\\q\"",          // invalid escape
  };

  for (const char* text : bad)
  {
    bool ok = true;
    JsonValue v = Json::Parse(text, &ok);
    EXPECT_FALSE(ok) << "expected rejection: " << text;
    EXPECT_TRUE(v.IsNull()) << "rejected parse must return Null: " << text;
  }
}

// ---- fuzz: hostile & truncated input ---------------------------------------

TEST(Json, GarbageBytesNeverCrash)
{
  // Fixed-seed LCG so the fuzz is deterministic and reproducible (the engine
  // forbids wall-clock RNG). Reaching the end without crashing is the pass.
  uint32_t rng = 0x5EED1234u;
  auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng; };

  for (int iter = 0; iter < 4000; ++iter)
  {
    std::string buf(next() % 64, '\0');
    for (char& c : buf)
      c = static_cast<char>(next() & 0xFF);

    bool ok = false;
    (void)Json::Parse(buf, &ok);   // may accept or reject; must not crash or over-read
  }
  SUCCEED();
}

TEST(Json, TruncatedInputIsRejectedWithoutCrashing)
{
  const std::string_view doc =
      R"({"ship":{"name":"Cobra","hp":128,"cargo":["ore","gems"],"docked":false,"pos":[1,-2,3.5]}})";

  bool ok = false;
  (void)Json::Parse(doc, &ok);
  EXPECT_TRUE(ok);   // the whole document is valid

  // Every strict prefix is a truncation: it must be handled cleanly (parsing a
  // partial value simply fails), and must never crash or read past the buffer.
  for (std::size_t n = 0; n < doc.size(); ++n)
  {
    bool prefixOk = true;
    JsonValue v = Json::Parse(doc.substr(0, n), &prefixOk);
    EXPECT_FALSE(prefixOk) << "prefix length " << n << " should be incomplete";
    EXPECT_TRUE(v.IsNull());
  }
}
