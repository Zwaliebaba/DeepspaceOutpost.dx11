# NeuronCore - shared engine core for the Neuron MMO migration.
#
# Foundation static library used by BOTH the client (DeepspaceOutpost via
# NeuronClient) and the server (Server via NeuronServer). It provides the
# standard building blocks the other modules build on: Debug (debug tracing +
# fatal/assert handling), FileSys (file reading), maths (GameMath/MathCommon),
# JSON, tasks and timing.

set(NEURONCORE_SOURCES
    NeuronCore.cpp
    FileSys.cpp
    Json.cpp
    TasksCore.cpp
    NetLib.cpp
)

set(NEURONCORE_HEADERS
    NeuronCore.h
    ECS.h
    Vector3i64.h
    Vector3d.h
    SpatialGrid.h
    DataWriter.h
    DataReader.h
    Replication.h
    SnapshotPacketizer.h
    SnapshotReceiver.h
    NetLib.h
    Debug.h
    FileSys.h
    GameMath.h
    MathCommon.h
    Json.h
    NeuronHelper.h
    TasksCore.h
    TimerCore.h
)

add_library(NeuronCore STATIC ${NEURONCORE_SOURCES} ${NEURONCORE_HEADERS})

target_compile_features(NeuronCore PUBLIC cxx_std_23)

# PUBLIC: consumers include "NeuronCore.h", "Debug.h", "FileSys.h", ... directly
# (e.g. via their own pch.h), so the include dir must propagate.
target_include_directories(NeuronCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Raw winsock UDP (NetLib) needs the Windows sockets library; PUBLIC so the
# linking executables (Server, client) pick it up transitively.
if(WIN32)
    target_link_libraries(NeuronCore PUBLIC ws2_32)
endif()

# Precompiled header (pch.h / pch.cpp). pch.h pulls in NeuronCore.h and the
# heavy Windows / C++/WinRT / winsock includes shared across the module.
target_precompile_headers(NeuronCore PRIVATE pch.h)
