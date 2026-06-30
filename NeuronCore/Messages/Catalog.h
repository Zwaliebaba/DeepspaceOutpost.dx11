#pragma once

// Catalog - include to register every catalog message into GlobalRegistry().
//
// Pulls in every message definition so their REGISTER_MESSAGE hooks populate the
// process-wide registry. Tools and tests include this single header to enumerate
// the whole wire ABI (schema export, catalog diff, the standalone packet decoder)
// without depending on the game executable.
//
// GalaxyManifest is intentionally absent from the generic registry: a manifest
// chunk is fixed-layout, hand-encoded display data carrying the reserved id
// Net::GALAXY_MANIFEST_ID (see GalaxyManifest.h), not a generic-codec message.

#include "Messages/Registry.h"

#include "Messages/Defs/InputCommand.h"
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/InputActions.h"
#include "StationProtocol.h"   // StationRequest / StationResponse (REGISTER_MESSAGE'd)
