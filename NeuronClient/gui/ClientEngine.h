#pragma once

// Output-size shim for the imported GUI (Phase 4).
//
// The donor GUI read the client area via ClientEngine::OutputSize(); the imported
// Canvas/GuiWindow use it for window placement and clamping. Back it with the
// native graphics core's output size.

#include "GraphicsCore.h"

class ClientEngine
{
  public:
    static Windows::Foundation::Size OutputSize() { return Neuron::Graphics::Core::GetOutputSize(); }
};
