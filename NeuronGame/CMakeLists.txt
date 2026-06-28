# NeuronGame : native Direct3D 11 shaders.
#
# Each Shaders/*.hlsl is compiled offline by fxc into a C++ byte-array header in
# CompiledShaders/<name>.h (array name g_<name>, with '-' replaced by '_'). The
# renderer (NeuronClient/ImmediateRenderer.cpp) #includes those headers, so
# NeuronClient depends on the NeuronGameShaders target produced here.
#
# Stage is taken from the file-name suffix: *VS.hlsl -> vs_5_0/VSMain,
# *PS.hlsl -> ps_5_0/PSMain. Partials (Shaders/partials/*.hlsli) are #include-only.

find_program(FXC_EXECUTABLE
  NAMES fxc
  HINTS
    "$ENV{WindowsSdkVerBinPath}x64"
    "$ENV{WindowsSdkVerBinPath}x86"
    "$ENV{WindowsSdkDir}bin/x64"
)

# The compiled byte-array headers (CompiledShaders/*.h) are committed to the repo,
# so a configure without fxc still builds against the prebuilt shaders. fxc is only
# needed to *regenerate* them after editing the HLSL. When it is missing we define
# the NeuronGameShaders target as a no-op pointing at the committed headers and warn,
# rather than failing the whole configure.
if(NOT FXC_EXECUTABLE)
  message(WARNING
    "fxc (the HLSL compiler) was not found - using the committed CompiledShaders/*.h "
    "byte arrays as-is. Editing Shaders/*.hlsl will NOT take effect until you configure "
    "from a Visual Studio / Windows SDK developer environment (or set FXC_EXECUTABLE).")
  file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl")
  file(GLOB SHADER_PARTIALS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/partials/*.hlsli")
  add_custom_target(NeuronGameShaders SOURCES ${SHADER_SOURCES} ${SHADER_PARTIALS})
  return()
endif()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CompiledShaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/*.hlsl")
file(GLOB SHADER_PARTIALS CONFIGURE_DEPENDS "${SHADER_SRC_DIR}/partials/*.hlsli")

# Per-shader fxc preprocessor defines (each entry "NAME=VALUE"). The lit programs are
# compiled with a fixed light count and per-vertex normals; lighting is then toggled at
# runtime through the b6 cbuffer. Keep MAX_LIGHTS in sync with Neuron::Graphics::MAX_LIGHTS
# in NeuronClient/ConstantBuffers.h.
set(SHADER_DEFINES_generic-colored-3dVS "MAX_LIGHTS=8" "NORMALS_PROVIDED=1")
set(SHADER_DEFINES_generic-colored-3dPS "MAX_LIGHTS=8" "NORMALS_PROVIDED=1")

set(SHADER_HEADERS "")
foreach(SHADER ${SHADER_SOURCES})
  get_filename_component(SHADER_NAME ${SHADER} NAME_WE)

  if(SHADER_NAME MATCHES "VS$")
    set(SHADER_PROFILE vs_5_0)
    set(SHADER_ENTRY VSMain)
  elseif(SHADER_NAME MATCHES "PS$")
    set(SHADER_PROFILE ps_5_0)
    set(SHADER_ENTRY PSMain)
  else()
    message(WARNING "Skipping shader with no VS/PS suffix: ${SHADER}")
    continue()
  endif()

  string(REPLACE "-" "_" SHADER_VAR "g_${SHADER_NAME}")
  set(SHADER_HEADER "${SHADER_OUT_DIR}/${SHADER_NAME}.h")

  set(SHADER_DEFINE_ARGS "")
  foreach(def ${SHADER_DEFINES_${SHADER_NAME}})
    list(APPEND SHADER_DEFINE_ARGS "/D" "${def}")
  endforeach()

  add_custom_command(
    OUTPUT "${SHADER_HEADER}"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ${SHADER_PROFILE} /E ${SHADER_ENTRY}
            ${SHADER_DEFINE_ARGS}
            /Fh "${SHADER_HEADER}" /Vn ${SHADER_VAR} "${SHADER}"
    DEPENDS "${SHADER}" ${SHADER_PARTIALS} "${CMAKE_CURRENT_LIST_FILE}"
    MAIN_DEPENDENCY "${SHADER}"
    COMMENT "fxc ${SHADER_NAME} -> CompiledShaders/${SHADER_NAME}.h"
    VERBATIM)

  list(APPEND SHADER_HEADERS "${SHADER_HEADER}")
endforeach()

add_custom_target(NeuronGameShaders DEPENDS ${SHADER_HEADERS}
  SOURCES ${SHADER_SOURCES} ${SHADER_PARTIALS})
