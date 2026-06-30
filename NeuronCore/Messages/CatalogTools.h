#pragma once

// CatalogTools - schema export and catalog diff over the message registry.
//
// Dev/CI tooling built on the single source of truth (MessageRegistry): emit the
// whole wire ABI as human-readable text (schema doc), and compare two catalogs to
// report what was added / removed / changed (a compatibility report - useful even
// under lockstep versioning, to SEE what changed and force a deliberate decision).
// Pure (registry in, string out), so it is unit-tested headlessly.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "MessageId.h"
#include "MessageTraits.h"
#include "Registry.h"

namespace Neuron::Msg
{
  [[nodiscard]] inline const char* ScopeName(MessageScope _s)
  {
    switch (_s)
    {
      case MessageScope::LocalOnly: return "LocalOnly";
      case MessageScope::Wire:      return "Wire";
      case MessageScope::Control:   return "Control";
      case MessageScope::DebugOnly: return "DebugOnly";
      case MessageScope::Tooling:   return "Tooling";
    }
    return "?";
  }

  [[nodiscard]] inline const char* KindName(MessageKind _k)
  {
    return _k == MessageKind::Command ? "Command" : "Event";
  }

  [[nodiscard]] inline const char* LaneName(MessageLane _l)
  {
    switch (_l)
    {
      case MessageLane::Control:    return "Control";
      case MessageLane::Gameplay:   return "Gameplay";
      case MessageLane::Bulk:       return "Bulk";
      case MessageLane::Unreliable: return "Unreliable";
    }
    return "?";
  }

  [[nodiscard]] inline const char* DirName(Direction _d)
  {
    switch (_d)
    {
      case Direction::None:           return "None";
      case Direction::ClientToServer: return "C->S";
      case Direction::ServerToClient: return "S->C";
      case Direction::Both:           return "Both";
    }
    return "?";
  }

  [[nodiscard]] inline std::string HexId(MessageId _id)
  {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(Raw(_id)));
    return buf;
  }

  // One catalog entry as a stable, single line.
  [[nodiscard]] inline std::string FormatEntry(const MessageInfo& _i)
  {
    std::string s = HexId(_i.id);
    s += "  ";
    s += _i.name;
    s += "  scope=";  s += ScopeName(_i.scope);
    s += " kind=";    s += KindName(_i.kind);
    s += " lane=";    s += LaneName(_i.lane);
    s += " dir=";     s += DirName(_i.dir);
    return s;
  }

  // The whole catalog as text, sorted by id (stable schema doc).
  [[nodiscard]] inline std::string ExportCatalogText(const MessageRegistry& _reg)
  {
    std::vector<MessageInfo> infos = _reg.All();
    std::sort(infos.begin(), infos.end(),
              [](const MessageInfo& _a, const MessageInfo& _b) { return Raw(_a.id) < Raw(_b.id); });

    std::string out;
    for (const MessageInfo& i : infos)
    {
      out += FormatEntry(i);
      out += '\n';
    }
    return out;
  }

  // --- Catalog diff (compatibility report between two catalogs) ----------------

  struct CatalogDiff
  {
    std::vector<MessageId> added;     // in new, not in old
    std::vector<MessageId> removed;   // in old, not in new
    std::vector<MessageId> changed;   // in both, but some trait differs
    [[nodiscard]] bool Empty() const { return added.empty() && removed.empty() && changed.empty(); }
  };

  [[nodiscard]] inline bool SameEntry(const MessageInfo& _a, const MessageInfo& _b)
  {
    return _a.id == _b.id && _a.scope == _b.scope && _a.kind == _b.kind
        && _a.lane == _b.lane && _a.dir == _b.dir && std::string(_a.name) == _b.name;
  }

  [[nodiscard]] inline CatalogDiff DiffCatalogs(const MessageRegistry& _old, const MessageRegistry& _new)
  {
    CatalogDiff diff;
    for (const MessageInfo& o : _old.All())
    {
      const MessageInfo* n = _new.Find(o.id);
      if (n == nullptr)
        diff.removed.push_back(o.id);
      else if (!SameEntry(o, *n))
        diff.changed.push_back(o.id);
    }
    for (const MessageInfo& n : _new.All())
      if (_old.Find(n.id) == nullptr)
        diff.added.push_back(n.id);
    return diff;
  }

  [[nodiscard]] inline std::string FormatDiff(const CatalogDiff& _d)
  {
    std::string out;
    for (MessageId id : _d.added)   { out += "+ "; out += HexId(id); out += '\n'; }
    for (MessageId id : _d.removed) { out += "- "; out += HexId(id); out += '\n'; }
    for (MessageId id : _d.changed) { out += "~ "; out += HexId(id); out += '\n'; }
    if (out.empty())
      out = "(no changes)\n";
    return out;
  }
}
