# Server - the dedicated server executable.
#
# A console application: Main.cpp is process startup + the fixed-tick loop;
# GameServer sequences the tick phases and the combat subscribers; WorldBuilder
# bootstraps the authoritative world; ServerConfig.h holds the tuning knobs.
# Generic engine plumbing (datagram pump, on-change cache) lives in NeuronServer.

# The SQL-Server (ODBC) persistence backend is Windows/soak-only and is NOT built
# by default, so CI needs no database or driver. Turn it on for a soak build with
# -DDSO_ENABLE_ODBC=ON; OdbcStore.cpp is then compiled (odbc32 auto-links via a
# #pragma). With it OFF the file is an empty TU and the store falls back to memory.
option(DSO_ENABLE_ODBC "Build the SQL Server (ODBC) persistence backend" OFF)

set(SERVER_SOURCES
    Main.cpp
    GameServer.cpp
    WorldBuilder.cpp
    SecureRandom.cpp
    OdbcStore.cpp
    pch.cpp
)

set(SERVER_HEADERS
    GameServer.h
    WorldBuilder.h
    ServerConfig.h
    SnapshotHelpers.h
    SecureRandom.h
    OdbcStore.h
    pch.h
)

add_executable(Server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_compile_features(Server PRIVATE cxx_std_23)

if(DSO_ENABLE_ODBC)
    target_compile_definitions(Server PRIVATE DSO_ENABLE_ODBC)
endif()

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
