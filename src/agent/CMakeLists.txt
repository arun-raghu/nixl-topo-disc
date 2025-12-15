# =============================================================================
# src/agent/CMakeLists.txt
# Agent component - distributed agent for performance measurement
# =============================================================================

set(AGENT_SOURCES
    # Add source files here as they are created
    # agent_main.cpp
    # bootstrap.cpp
    # nixl_comm.cpp
    # perf_tests.cpp
)

# Create agent executable (placeholder until sources exist)
# Uncomment when source files are added:
# add_executable(agent ${AGENT_SOURCES})

# For now, create a placeholder target
add_custom_target(agent_placeholder
    COMMENT "Agent component - add source files to build"
)

# When source files are added, uncomment:
# add_executable(agent ${AGENT_SOURCES})
# add_executable(nixl_topo::agent ALIAS agent)
#
# target_include_directories(agent PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )
#
# target_link_libraries(agent PRIVATE
#     nixl_topo::common
#     Threads::Threads
#     $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
#     $<$<BOOL:${WITH_UCX}>:UCX::UCX>
#     $<$<BOOL:${WITH_GDR}>:GDR::gdrapi>
#     $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
#     $<$<BOOL:${ENABLE_CUDA}>:CUDA::cudart>
# )
#
# # Installation
# install(TARGETS agent
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )
