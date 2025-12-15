# =============================================================================
# src/topology/CMakeLists.txt
# Topology component - topology discovery and representation
# =============================================================================

set(TOPOLOGY_SOURCES
    # Add source files here as they are created
    # topology_discovery.cpp
    # topology_graph.cpp
)

# For now, create a placeholder target
add_custom_target(topology_placeholder
    COMMENT "Topology component - add source files to build"
)

# When source files are added, create a library:
# add_library(topology STATIC ${TOPOLOGY_SOURCES})
# add_library(nixl_topo::topology ALIAS topology)
#
# target_include_directories(topology PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
#     $<INSTALL_INTERFACE:include>
# )
#
# target_link_libraries(topology PUBLIC
#     nixl_topo::common
#     $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
# )
#
# # Installation
# install(TARGETS topology
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
