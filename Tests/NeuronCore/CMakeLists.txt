# NeuronCore.Tests - GoogleTest project for the NeuronCore static library.
#
# Exercises the header-only engine pieces NeuronCore exposes: the in-house ECS,
# int64 world math, the spatial grid, and the serialization / replication /
# reliable-channel building blocks. NeuronCore propagates its source dir as a
# PUBLIC include directory, so those headers are reachable directly.

add_executable(NeuronCore.Tests
    EcsTests.cpp
    MathTests.cpp
    SpatialTests.cpp
    ClientInputTests.cpp
    ReliableChannelTests.cpp
    StationProtocolTests.cpp
    InterpolationTests.cpp
    SnapshotTransportTests.cpp
)

target_compile_features(NeuronCore.Tests PRIVATE cxx_std_23)

# Link the library under test plus GoogleTest's bundled main().
target_link_libraries(NeuronCore.Tests PRIVATE NeuronCore GTest::gtest_main)

if(MSVC)
    target_compile_options(NeuronCore.Tests PRIVATE /W4)
    target_compile_definitions(NeuronCore.Tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Register every TEST() with CTest so the `ctest` step in CI runs them.
gtest_discover_tests(NeuronCore.Tests)
