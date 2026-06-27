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

# Include sub-projects.
#
# Engine modules for the MMO migration. NeuronCore is the shared foundation;
# NeuronClient and NeuronServer build on it. Declare the libraries before their
# consumers (DeepspaceOutpost = client, Server = dedicated server).
add_subdirectory ("NeuronCore")
add_subdirectory ("NeuronClient")
add_subdirectory ("NeuronServer")

add_subdirectory ("DeepspaceOutpost")
add_subdirectory ("Server")

# Headless unit / golden-run tests (run by CTest in CI).
add_subdirectory ("Tests")
