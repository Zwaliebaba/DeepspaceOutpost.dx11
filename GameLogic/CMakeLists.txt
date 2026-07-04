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
    Broadphase.h
    MotionSystem.h
    FlightInput.h
    FlightSystem.h
    Economy.h
    Galaxy.h
    GalaxyGen.h
    Combat.h
    SnapshotBuilder.h
    AreaOfInterest.h
    DespawnTracker.h
    ServerSessions.h
    CombatSystem.h
    MissileSystem.h
    AiSystem.h
    CollisionSystem.h
    HyperspaceSystem.h
    EquipmentSystem.h
    KillRewards.h
    LootSystem.h
    SpawnDirector.h
    StationServices.h
    CombatMessages.h
    PlayerPersistence.h
)

add_library(GameLogic STATIC ${GAMELOGIC_SOURCES} ${GAMELOGIC_HEADERS})

target_compile_features(GameLogic PUBLIC cxx_std_23)

if(MSVC)
    # /fp:strict pins IEEE evaluation order/contraction so the simulation stays
    # bit-identical across builds - the golden-run tests and future replays
    # depend on it (ARCHITECTURE.md 13.3-E5). PUBLIC: every TU that inlines
    # GameLogic's header-only systems (Server, the test binaries) must compile
    # them under the same floating-point contract.
    target_compile_options(GameLogic PRIVATE /W4)
    target_compile_options(GameLogic PUBLIC /fp:strict)
endif()

# PUBLIC so consumers (Server, Tests) get GameLogic's headers and the header-only
# NeuronCore engine headers it builds on. NeuronServer is on the path for the
# header-only PlayerPersistState struct that PlayerPersistence.h converts to/from
# (a POD only - GameLogic does not link the NeuronServer library).
target_include_directories(GameLogic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/NeuronCore
    ${CMAKE_SOURCE_DIR}/NeuronServer
)
