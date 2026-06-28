# Server - standalone dedicated server executable (placeholder for now).
#
# A console application that drives the engine via NeuronServer + NeuronCore.

set(SERVER_SOURCES
    Main.cpp
    pch.cpp
)

set(SERVER_HEADERS
    pch.h
)

add_executable(Server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_compile_features(Server PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(Server PRIVATE /W4)
endif()

# Server uses NeuronServer + NeuronCore for the engine, and GameLogic for the
# authoritative simulation it ticks.
target_link_libraries(Server PRIVATE NeuronServer NeuronCore GameLogic)

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(Server PRIVATE pch.h)
