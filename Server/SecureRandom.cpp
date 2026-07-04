// SecureRandom - see SecureRandom.h. The Windows CSPRNG (bcrypt) is contained to
// this one TU. The precompiled header (NeuronCore.h) already pulls <windows.h> in
// via <winsock2.h> with WIN32_LEAN_AND_MEAN/NOMINMAX set, so only <bcrypt.h> is new.

#include "pch.h"

#include "SecureRandom.h"

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")   // auto-link; no CMake/vcxproj change needed

namespace DSOServer
{
  uint64_t SecureRandom64()
  {
    // BCryptGenRandom with the system-preferred RNG is the OS CSPRNG and, per the
    // Windows docs, does not fail in practice; a failed draw or the ~never all-zero
    // result simply retries so the token is always strong and nonzero.
    uint64_t v = 0;
    do
    {
      const NTSTATUS st = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v),
                                          static_cast<ULONG>(sizeof(v)),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
      if (!BCRYPT_SUCCESS(st))
        v = 0;
    } while (v == 0);
    return v;
  }
}
