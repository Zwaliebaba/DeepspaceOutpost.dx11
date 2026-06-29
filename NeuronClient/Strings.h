#pragma once

// Localized string table (Phase 3 of the GUI/text import).
//
// Strings are loaded from JSON shipped under GameData/Strings/<language>/<class>.json
// (each file a flat object mapping id -> localized value). Imported from the donor
// and adapted to the target: it reads through the same CWD-relative asset convention
// the rest of the game uses and parses with Neuron::Json, with no WinRT MRT /
// Globalization dependency.

#include <string>

class Strings
{
  public:
    static void Startup();
    static void Shutdown();
    static void SetLanguage(const std::wstring& _language);

    static std::wstring Get(const std::wstring& _stringId, const std::wstring& _class = L"Strings");
    static std::string Get(const std::string& _stringId, const std::string& _class = "Strings");
};
