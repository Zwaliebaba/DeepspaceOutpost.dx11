#pragma once

// ChatModeration - server-side chat rate limiting + sanitisation (GameLogic, G3).
//
// Chat relays a player's line to everyone; two pure gates protect that broadcast:
// a per-session RATE limit (a small number of lines per rolling window, so one
// client can't flood the channel) and text SANITISATION (strip control bytes, cap
// length, trim) applied server-side regardless of what the client sent. Both are
// pure and unit-tested; the server (GameServer::HandleChat) owns a ChatLimiter per
// session and stamps the authenticated sender before rebroadcasting.

#include <cstdint>
#include <string>

namespace Neuron::GameLogic
{
  inline constexpr uint32_t    CHAT_WINDOW_TICKS   = 300;   // ~10 s at 30 Hz
  inline constexpr int         CHAT_MAX_PER_WINDOW = 6;     // lines allowed per window
  inline constexpr std::size_t CHAT_MAX_LEN        = 200;   // hard cap on a relayed line

  // Per-session rate state: a count that resets each window. The window anchors on
  // the first line (rather than at tick 0), so every window is a full width.
  struct ChatLimiter
  {
    uint32_t windowStart = 0;
    int      count = 0;
    bool     started = false;
  };

  // May this session send a chat line at `_tick`? Records the line on success;
  // returns false once it has hit CHAT_MAX_PER_WINDOW within the current window.
  [[nodiscard]] inline bool ChatAllowed(ChatLimiter& _lim, uint32_t _tick)
  {
    if (!_lim.started || _tick - _lim.windowStart >= CHAT_WINDOW_TICKS)   // tick is monotonic, so no wrap
    {
      _lim.windowStart = _tick;
      _lim.count = 0;
      _lim.started = true;
    }
    if (_lim.count >= CHAT_MAX_PER_WINDOW)
      return false;
    ++_lim.count;
    return true;
  }

  // Clean a chat line: drop control bytes (< 0x20), keep printable ASCII and UTF-8
  // (>= 0x80), cap to CHAT_MAX_LEN, and trim trailing spaces. Returns the cleaned
  // text (empty if nothing printable survives - the caller drops an empty line).
  [[nodiscard]] inline std::string SanitizeChat(const std::string& _raw)
  {
    std::string out;
    for (char c : _raw)
    {
      const unsigned char u = static_cast<unsigned char>(c);
      if (u >= 0x20)   // printable ASCII or a UTF-8 byte; control chars dropped
        out.push_back(c);
      if (out.size() >= CHAT_MAX_LEN)
        break;
    }
    while (!out.empty() && out.back() == ' ')
      out.pop_back();
    return out;
  }
}
