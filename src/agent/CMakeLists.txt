# =============================================================================
# src/agent/CMakeLists.txt
# Agent component - distributed agent for performance measurement
# =============================================================================

set(AGENT_SOURCES
    agent_main.cpp
    agent.cpp
)

# Create agent executable
add_executable(agent ${AGENT_SOURCES})
add_executable(nixl_topo::agent ALIAS agent)

target_include_directories(agent PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(agent PRIVATE
    nixl_topo::common
    Threads::Threads
    $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
)

# Installation
install(TARGETS agent
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
