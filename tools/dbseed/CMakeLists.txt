# dbseed - the galaxy seeding tool (v2). Windows/ODBC only.
#
# Reuses the server's OdbcStore for the SQL writes and the shared GalaxyRows.h
# conversions, so the rows it seeds are exactly the rows the server would generate
# from the seed. Guarded by DSO_ENABLE_ODBC at the root, so a default (CI)
# configure never builds it - same gate as the server's ODBC backend.

add_executable(dbseed
    dbseed.cpp
    ${CMAKE_SOURCE_DIR}/Server/OdbcStore.cpp
)

target_compile_features(dbseed PRIVATE cxx_std_23)
target_compile_definitions(dbseed PRIVATE DSO_ENABLE_ODBC)

# Server/ for OdbcStore.{h,cpp}, GalaxyRows.h and the Server pch.h that OdbcStore
# includes; the engine libs for the generator + the persistence store interface.
target_include_directories(dbseed PRIVATE ${CMAKE_SOURCE_DIR}/Server)
target_link_libraries(dbseed PRIVATE NeuronServer NeuronCore GameLogic)

if(MSVC)
    target_compile_options(dbseed PRIVATE /W4)
endif()
