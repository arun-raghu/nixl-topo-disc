# =============================================================================
# src/harness/CMakeLists.txt
# Harness component - test harness and benchmarking utilities
# =============================================================================

set(HARNESS_SOURCES
    # Add source files here as they are created
    # harness_main.cpp
    # benchmark_runner.cpp
)

# For now, create a placeholder target
add_custom_target(harness_placeholder
    COMMENT "Harness component - add source files to build"
)

# When source files are added, create an executable:
# add_executable(harness ${HARNESS_SOURCES})
# add_executable(nixl_topo::harness ALIAS harness)
#
# target_include_directories(harness PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )
#
# target_link_libraries(harness PRIVATE
#     nixl_topo::common
#     nixl_topo::topology
#     Threads::Threads
#     $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
#     $<$<BOOL:${WITH_UCX}>:UCX::UCX>
#     $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
# )
#
# # Installation
# install(TARGETS harness
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )
