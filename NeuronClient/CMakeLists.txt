# NeuronClient - client-side engine layer for the Neuron MMO migration.
#
# Built on NeuronCore and consumed by the DeepspaceOutpost game executable.
# Placeholder content for now (just the precompiled-header translation unit);
# client-only systems land here as the migration progresses.

set(NEURONCLIENT_SOURCES
    pch.cpp
)

set(NEURONCLIENT_HEADERS
    NeuronClient.h
    pch.h
)

add_library(NeuronClient STATIC ${NEURONCLIENT_SOURCES} ${NEURONCLIENT_HEADERS})

target_compile_features(NeuronClient PUBLIC cxx_std_23)

# PUBLIC so the DeepspaceOutpost target can include NeuronClient headers.
target_include_directories(NeuronClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# NeuronClient is built on top of NeuronCore; PUBLIC so the core is transitively
# available to anything that links NeuronClient.
target_link_libraries(NeuronClient PUBLIC NeuronCore)

# Precompiled header (pch.h / pch.cpp).
target_precompile_headers(NeuronClient PRIVATE pch.h)
