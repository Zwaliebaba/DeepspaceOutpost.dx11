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

# Server uses NeuronServer and NeuronCore. (NeuronServer already links NeuronCore
# publicly; NeuronCore is listed explicitly per the module dependency contract.)
target_link_libraries(Server PRIVATE NeuronServer NeuronCore)

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(Server PRIVATE pch.h)
