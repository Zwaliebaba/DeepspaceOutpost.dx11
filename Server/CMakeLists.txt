# Server - the dedicated server executable.
#
# A console application: Main.cpp is process startup + the fixed-tick loop;
# GameServer sequences the tick phases and the combat subscribers; WorldBuilder
# bootstraps the authoritative world; ServerConfig.h holds the tuning knobs.
# Generic engine plumbing (datagram pump, on-change cache) lives in NeuronServer.

set(SERVER_SOURCES
    Main.cpp
    GameServer.cpp
    WorldBuilder.cpp
    SecureRandom.cpp
    pch.cpp
)

set(SERVER_HEADERS
    GameServer.h
    WorldBuilder.h
    ServerConfig.h
    SnapshotHelpers.h
    SecureRandom.h
    pch.h
)

add_executable(Server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_compile_features(Server PRIVATE cxx_std_23)

if(MSVC)
    # /fp:strict rides in transitively from GameLogic (PUBLIC) so the server's
    # own TUs, which inline the header-only simulation, match its floating-point
    # contract exactly.
    target_compile_options(Server PRIVATE /W4)
endif()

# Server uses NeuronServer + NeuronCore for the engine, and GameLogic for the
# authoritative simulation it ticks.
target_link_libraries(Server PRIVATE NeuronServer NeuronCore GameLogic)

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(Server PRIVATE pch.h)
