# Tests/ - per-library GoogleTest projects.
#
# One companion "<Library>.Tests" executable per engine library, each registering
# its cases with CTest via gtest_discover_tests(). These were migrated from the
# original single hand-rolled Tests/ executable (TestFramework.h): every source
# file now lives under the library whose code it exercises.
#
# GoogleTest itself is fetched once at the top level (see the root CMakeLists);
# this directory only consumes the GTest::gtest_main target.

add_subdirectory(NeuronCore)
add_subdirectory(NeuronClient)
add_subdirectory(NeuronServer)
add_subdirectory(GameLogic)
