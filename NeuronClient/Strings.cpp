#include "pch.h"
#include "Strings.h"
#include "Json.h"

#include <fstream>
#include <iterator>
#include <map>

// String resources are loaded from the .json files shipped under
// GameData/Strings/<language>/<class>.json (staged next to the executable, the same
// CWD-relative convention the rest of the game's assets use). Each file is a flat
// JSON object mapping a resource id to its localized value.

namespace
{
  std::wstring g_language = L"en-US";

  // Cache of loaded resource classes for the current language:
  //   class name ("Strings") -> ( resource id -> value ).
  std::map<std::wstring, std::map<std::wstring, std::wstring>> g_classes;

  std::wstring Utf8ToWide(const std::string& _utf8)
  {
    if (_utf8.empty())
      return {};

    const int required = ::MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), nullptr, 0);
    if (required <= 0)
      return {};

    std::wstring result(static_cast<size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), result.data(), required);
    return result;
  }

  std::string WideToUtf8(const std::wstring& _wide)
  {
    if (_wide.empty())
      return {};

    const int required = ::WideCharToMultiByte(CP_UTF8, 0, _wide.data(), static_cast<int>(_wide.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
      return {};

    std::string result(static_cast<size_t>(required), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, _wide.data(), static_cast<int>(_wide.size()), result.data(), required, nullptr, nullptr);
    return result;
  }

  std::string ReadFileBytes(const std::wstring& _path)
  {
    std::ifstream file(_path.c_str(), std::ios::binary); // MSVC: wide-path ifstream overload
    if (!file)
      return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  }

  const std::map<std::wstring, std::wstring>& LoadClass(const std::wstring& _class)
  {
    if (const auto it = g_classes.find(_class); it != g_classes.end())
      return it->second;

    const std::wstring relativePath = L"Strings/" + g_language + L"/" + _class + L".json";
    const std::string text = ReadFileBytes(relativePath);

    std::map<std::wstring, std::wstring> table;
    if (!text.empty())
    {
      bool ok = false;
      const JsonValue root = Json::Parse(text, &ok);
      if (ok && root.IsObject())
      {
        for (const auto& [id, value] : root.AsObject())
        {
          if (value.IsString())
            table.emplace(Utf8ToWide(id), Utf8ToWide(value.AsString()));
        }
      }
    }

    return g_classes.emplace(_class, std::move(table)).first->second;
  }
}

void Strings::Startup()
{
  // Default to en-US; callers may override with SetLanguage().
  SetLanguage(L"en-US");
}

void Strings::Shutdown() { g_classes.clear(); }

void Strings::SetLanguage(const std::wstring& _language)
{
  if (!_language.empty())
    g_language = _language;

  // Force a reload of every cached class against the new language.
  g_classes.clear();
}

std::wstring Strings::Get(const std::wstring& _stringId, const std::wstring& _class)
{
  const std::map<std::wstring, std::wstring>& table = LoadClass(_class);

  if (const auto it = table.find(_stringId); it != table.end())
    return it->second;

  // Fall back to the fully-qualified key, matching the previous behaviour.
  return _class + L"/" + _stringId;
}

std::string Strings::Get(const std::string& _stringId, const std::string& _class)
{
  return WideToUtf8(Get(Utf8ToWide(_stringId), Utf8ToWide(_class)));
}
