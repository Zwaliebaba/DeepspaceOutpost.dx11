# NeuronServer - server-side engine layer for the Neuron MMO migration.
#
# Built on NeuronCore and consumed by the Server executable. Placeholder content
# for now (the precompiled-header translation unit plus the NeuronServer header);
# server-only systems land here as the migration progresses.

set(NEURONSERVER_SOURCES
    pch.cpp
)

set(NEURONSERVER_HEADERS
    NeuronServer.h
    pch.h
)

add_library(NeuronServer STATIC ${NEURONSERVER_SOURCES} ${NEURONSERVER_HEADERS})

target_compile_features(NeuronServer PUBLIC cxx_std_23)

# PUBLIC so the Server target can include NeuronServer headers.
target_include_directories(NeuronServer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# NeuronServer is built on top of NeuronCore; PUBLIC so the core is transitively
# available to anything that links NeuronServer.
target_link_libraries(NeuronServer PUBLIC NeuronCore)

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(NeuronServer PRIVATE pch.h)
