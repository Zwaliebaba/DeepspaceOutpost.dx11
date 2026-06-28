# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

project ("DeepspaceOutpost" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# ---------------------------------------------------------------------------
# Unit-test infrastructure (GoogleTest).
#
# Each engine library has a companion GoogleTest project (a *.Tests target under
# the matching Tests/<Library>/ subdirectory). GoogleTest is fetched at configure
# time and each test binary registers itself with CTest via
# gtest_discover_tests(), so the `ctest` step in CI picks them all up with no
# per-binary wiring.
#
# Test code is the single sanctioned exception to the repo's "native-first / no
# third-party" rule (see AGENTS.md): GoogleTest is a build-time dependency of the
# test binaries only and never ships in the engine or game executables. The
# former hand-rolled Tests/ executable (TestFramework.h) has been migrated into
# these per-library GoogleTest projects under Tests/.
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build the unit tests" ON)

if(BUILD_TESTING)
    include(FetchContent)

    # Use the archive's extraction time as the timestamp (CMP0135 NEW) so the
    # pinned tarball re-extracts cleanly; silences the FetchContent dev warning.
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    # Match MSVC's dynamic CRT (/MD) so GoogleTest links against the same runtime
    # as the engine libraries under test (avoids LNK2038 CRT-mismatch errors).
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Pinned release tarball (not a git clone): reproducible, dependency-light,
    # and verified by hash. Update the URL + URL_HASH together to bump versions.
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
    )
    FetchContent_MakeAvailable(googletest)

    # Provides gtest_discover_tests().
    include(GoogleTest)
endif()

# Include sub-projects.
#
# Engine modules for the MMO migration. NeuronCore is the shared foundation;
# NeuronClient and NeuronServer build on it. Declare the libraries before their
# consumers (DeepspaceOutpost = client, Server = dedicated server).
add_subdirectory ("NeuronCore")
add_subdirectory ("NeuronClient")
add_subdirectory ("NeuronServer")

# Server-only authoritative simulation (headless ECS systems).
add_subdirectory ("GameLogic")

add_subdirectory ("DeepspaceOutpost")
add_subdirectory ("Server")

# Per-library GoogleTest projects (run by CTest in CI). Migrated from the former
# hand-rolled Tests/ executable; see Tests/ for the per-module breakdown. Guarded
# by BUILD_TESTING so a tests-off configure skips them (and the GoogleTest fetch).
if(BUILD_TESTING)
    add_subdirectory ("Tests")
endif()
