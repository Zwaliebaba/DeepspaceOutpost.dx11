# GameLogic.Tests - GoogleTest project for the GameLogic static library.
#
# Covers the server-only authoritative systems (motion/flight, combat, economy,
# galaxy, spawning, sessions, station services, AOI/replication). GameLogic
# propagates both its own and NeuronCore's include directories (PUBLIC), so the
# game-system headers and the engine headers they build on are reachable.

add_executable(GameLogic.Tests
    AreaOfInterestTests.cpp
    CombatSystemTests.cpp
    CombatTests.cpp
    EconomyTests.cpp
    FlightInputTests.cpp
    FlightTests.cpp
    GalaxyGenTests.cpp
    GalaxyTests.cpp
    MotionTests.cpp
    SessionTests.cpp
    SpawnDirectorTests.cpp
    StationServicesTests.cpp
    ReplicationTests.cpp
    GalaxyManifestTests.cpp
)

target_compile_features(GameLogic.Tests PRIVATE cxx_std_23)

target_link_libraries(GameLogic.Tests PRIVATE GameLogic GTest::gtest_main)

if(MSVC)
    target_compile_options(GameLogic.Tests PRIVATE /W4)
    target_compile_definitions(GameLogic.Tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

gtest_discover_tests(GameLogic.Tests)
