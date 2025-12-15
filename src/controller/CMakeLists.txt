# =============================================================================
# src/controller/CMakeLists.txt
# Controller component - orchestrates agents and collects results
# =============================================================================

set(CONTROLLER_SOURCES
    # Add source files here as they are created
    # controller_main.cpp
    # rendezvous.cpp
    # test_manager.cpp
    # data_collector.cpp
)

# Create controller executable (placeholder until sources exist)
# Uncomment when source files are added:
# add_executable(controller ${CONTROLLER_SOURCES})

# For now, create a placeholder target
add_custom_target(controller_placeholder
    COMMENT "Controller component - add source files to build"
)

# When source files are added, uncomment:
# add_executable(controller ${CONTROLLER_SOURCES})
# add_executable(nixl_topo::controller ALIAS controller)
#
# target_include_directories(controller PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )
#
# target_link_libraries(controller PRIVATE
#     nixl_topo::common
#     Threads::Threads
#     $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
#     $<$<BOOL:${WITH_UCX}>:UCX::UCX>
#     $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
# )
#
# # Installation
# install(TARGETS controller
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )
