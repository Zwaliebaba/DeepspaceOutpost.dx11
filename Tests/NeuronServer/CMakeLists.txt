# NeuronServer.Tests - GoogleTest project for the NeuronServer static library.
#
# The original hand-rolled suite had no server-engine unit tests (NeuronServer is
# still a placeholder layer; see AGENTS.md). This project starts with a build/link
# smoke test so the server-only systems (sessions, AOI/replication, persistence)
# have a home to land in as the migration progresses.

add_executable(NeuronServer.Tests
    SmokeTests.cpp
)

target_compile_features(NeuronServer.Tests PRIVATE cxx_std_23)

target_link_libraries(NeuronServer.Tests PRIVATE NeuronServer GTest::gtest_main)

if(MSVC)
    target_compile_options(NeuronServer.Tests PRIVATE /W4)
endif()

gtest_discover_tests(NeuronServer.Tests)
