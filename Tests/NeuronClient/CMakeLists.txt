# NeuronClient.Tests - GoogleTest project for client-side presentation code.
#
# These tests exercise client headers (camera follow, the client Universe view,
# the replicated-scene applier) that still physically live in the
# DeepspaceOutpost client project pending their migration into NeuronClient, so
# the DeepspaceOutpost (and GameLogic, for shared component schemas) source dirs
# are added to the include path. NeuronClient propagates NeuronCore's includes.

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
