# Tests - the project's unit/golden-run test executable.
#
# A standalone console exe with a tiny hand-rolled framework (TestFramework.h),
# run by CTest in CI. It is dependency-light on purpose: it includes the
# header-only NeuronCore engine pieces (e.g. ECS.h) directly rather than linking
# the full NeuronCore static lib, so the tests stay fast and headless.

set(TEST_SOURCES
    TestMain.cpp
    EcsTests.cpp
    UniverseTests.cpp
    MathTests.cpp
    SpatialTests.cpp
    MotionTests.cpp
    EconomyTests.cpp
)

add_executable(Tests ${TEST_SOURCES} TestFramework.h)

target_compile_features(Tests PRIVATE cxx_std_23)

# Header-only access to the engine code under test: ECS.h lives in NeuronCore;
# the game components / Universe + vector.h live in the DeepspaceOutpost project
# (they relocate to GameLogic at A4). All are header-only, so no linking needed.
target_include_directories(Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/NeuronCore
    ${CMAKE_SOURCE_DIR}/DeepspaceOutpost
    ${CMAKE_SOURCE_DIR}/GameLogic
)

if(MSVC)
    target_compile_options(Tests PRIVATE /W4)
    target_compile_definitions(Tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME unit COMMAND Tests)
