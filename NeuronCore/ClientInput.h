#pragma once

// ClientInput - compatibility alias for the InputCommand catalog message (NeuronCore).
//
// The client -> server intent used to be a bespoke 'NCMD' packet with its own
// WriteInput/ReadInput codec. It is now the InputCommand message (see
// Messages/Defs/InputCommand.h), carried on the unified 'NMSG' UNRELIABLE lane and
// encoded by the generic Serialize codec. This header keeps the historical
// Net::ClientInput name and the NO_MISSILE_TARGET sentinel as thin aliases so the
// existing call sites (server OnInput, the client input builder) need no change.

#include "Messages/Defs/InputCommand.h"

namespace Neuron::Net
{
  // The intent struct is the InputCommand message; the bespoke codec is gone.
  using ClientInput = Msg::InputCommand;

  inline constexpr uint32_t NO_MISSILE_TARGET = Msg::NO_MISSILE_TARGET;
}
