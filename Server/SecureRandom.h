#pragma once

// SecureRandom - OS cryptographic RNG for session tokens (Server).
//
// B2 session tokens must be unguessable, so they are drawn from the operating
// system's cryptographic RNG - deliberately NOT a gameplay LCG stream (those are
// deterministic by design, and determinism rules apply only inside GameLogic). This
// is server-only: GameLogic stays pure and pulls its tokens through
// ServerSessions::SetTokenSource, which the server points at SecureRandom64.

#include <cstdint>

namespace DSOServer
{
  // A cryptographically-strong, nonzero 64-bit value for a session token. Never
  // returns 0 (0 means "unauthenticated" on the wire).
  [[nodiscard]] uint64_t SecureRandom64();
}
