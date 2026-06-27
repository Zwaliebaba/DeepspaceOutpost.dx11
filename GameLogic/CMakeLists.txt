# GameLogic - the server-only authoritative simulation (headless, A4).
#
# Holds all game behaviour as ECS systems over the int64 world. Deliberately
# light: it uses only the header-only NeuronCore engine pieces (ECS.h,
# Vector3i64.h) via the include path, and does NOT link the WinRT-heavy
# NeuronCore library, so it stays headless and cheap. The dedicated Server links
# it; the client links none of it.

set(GAMELOGIC_SOURCES
    GameLogic.cpp
)

set(GAMELOGIC_HEADERS
    GameLogic.h
    SimComponents.h
    MotionSystem.h
    FlightInput.h
    FlightSystem.h
    Economy.h
    Galaxy.h
    Combat.h
    SnapshotBuilder.h
    AreaOfInterest.h
    DespawnTracker.h
    ServerSessions.h
    CombatSystem.h
    StationServices.h
)

add_library(GameLogic STATIC ${GAMELOGIC_SOURCES} ${GAMELOGIC_HEADERS})

target_compile_features(GameLogic PUBLIC cxx_std_23)

# PUBLIC so consumers (Server, Tests) get GameLogic's headers and the header-only
# NeuronCore engine headers it builds on.
target_include_directories(GameLogic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/NeuronCore
)
