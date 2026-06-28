# NeuronClient.Tests - GoogleTest project for client-side presentation code.
#
# Camera-follow, the replicated-scene applier and the projection optics now live
# in the NeuronClient library and are reached through its public include dirs
# (via the link below). The DeepspaceOutpost source dir is still on the include
# path for the headers that remain game-specific (the client Universe view), and
# GameLogic for shared component schemas. NeuronClient propagates NeuronCore's
# includes.

add_executable(NeuronClient.Tests
    CameraFollowTests.cpp
    UniverseTests.cpp
    ReplicatedSceneTests.cpp
    ViewMetricsTests.cpp
)

target_compile_features(NeuronClient.Tests PRIVATE cxx_std_23)

target_link_libraries(NeuronClient.Tests PRIVATE NeuronClient GTest::gtest_main)

# The headers under test still live in the client exe / game-logic source trees.
target_include_directories(NeuronClient.Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/DeepspaceOutpost
    ${CMAKE_SOURCE_DIR}/GameLogic
)

if(MSVC)
    target_compile_options(NeuronClient.Tests PRIVATE /W4)
endif()

gtest_discover_tests(NeuronClient.Tests)
