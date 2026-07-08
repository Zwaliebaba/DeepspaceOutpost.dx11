# BotClient - the headless load/soak harness (D5, roadmap #20).
#
# A console app that drives N scripted sessions over the real UDP stack using the
# production ReplicationClient (NeuronClient), with no graphics/audio/window. Also
# registered as the CI smoke test: it spawns the Server on a side port, runs 8
# bots on loopback for ~20 s, asserts every bot connects/streams/completes the
# chart pull, and parses the server's [metrics] lines for tick overruns.

add_executable(BotClient Main.cpp)

target_compile_features(BotClient PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(BotClient PRIVATE /W4)
endif()

# ReplicationClient lives in NeuronClient (which PUBLIC-links NeuronCore and the
# system libs; none of the D3D/XAudio surface is initialized by this target).
target_link_libraries(BotClient PRIVATE NeuronClient)

# The CI smoke lane (runs under `ctest` after the build): server + 8 bots on
# loopback port 40123. A small overrun tolerance absorbs hosted-runner stalls -
# a hard zero would make the lane flaky without telling us anything real.
add_test(NAME BotClient.Smoke
         COMMAND BotClient --smoke --server-exe $<TARGET_FILE:Server>
                 --bots 8 --seconds 20 --port 40123 --max-overruns 3)
set_tests_properties(BotClient.Smoke PROPERTIES TIMEOUT 120)

# Adverse-condition lane: same fleet, but every bot is forced through one full
# reconnect (fresh session, real teardown + re-handshake + galaxy re-pull over live
# UDP) ~8 s in, then must recover to the same verdict. A different port from the
# plain smoke lane so the two can run concurrently under `ctest -j`. Longer run
# (24 s) leaves ample time to recover after the churn; timeout raised to match.
add_test(NAME BotClient.Smoke.Churn
         COMMAND BotClient --smoke --server-exe $<TARGET_FILE:Server>
                 --bots 8 --seconds 24 --port 40124 --max-overruns 3 --churn-seconds 8)
set_tests_properties(BotClient.Smoke.Churn PROPERTIES TIMEOUT 150)
