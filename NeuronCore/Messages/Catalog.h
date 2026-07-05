#pragma once

// Catalog - include to register every catalog message into GlobalRegistry().
//
// Pulls in every message definition so their REGISTER_MESSAGE hooks populate the
// process-wide registry. Tools and tests include this single header to enumerate
// the whole wire ABI (schema export, catalog diff, the standalone packet decoder)
// without depending on the game executable.
//
// Retired ids stay reserved forever (permanent ABI): 0x0210 was the hand-encoded
// galaxy-manifest chunk, replaced by the catalog-codec GalaxyChunk (0x1003).

#include "Messages/Registry.h"

#include "Messages/Defs/InputCommand.h"
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/InputActions.h"
#include "Messages/Defs/PlayerSession.h"   // ClientHello / PlayerInfo / PlayerStatus
#include "Messages/Defs/EquipmentEvents.h"  // EcmPulse / EscapePodUsed
#include "Messages/Defs/Travel.h"           // TravelRequest / TravelResponse
#include "Messages/Defs/GalaxyChunks.h"     // GalaxyChunkRequest / GalaxyChunk
#include "Messages/Defs/TimeSync.h"         // Ping / Pong (E1 time sync)
#include "Messages/Defs/Strategic.h"        // StrategicSummary (E3 strategic tier)
#include "Messages/Defs/ExplosionAt.h"      // ExplosionAt (G1 kill VFX broadcast)
#include "StationProtocol.h"   // StationRequest / StationResponse (REGISTER_MESSAGE'd)
