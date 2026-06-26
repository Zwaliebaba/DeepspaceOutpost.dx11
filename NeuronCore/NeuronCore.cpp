#include "pch.h"

void CoreEngine::Startup()
{
  if (!XMVerifyCPUSupport())
    Fatal(L"CPU does not support the right technology");

  Timer::Core::Startup();
  Tasks::Core::Startup();
}

void CoreEngine::Shutdown()
{
  Tasks::Core::Shutdown();
}
