# =============================================================================
# src/topology/CMakeLists.txt
# Topology component - topology discovery and representation
# =============================================================================

set(TOPOLOGY_SOURCES
    tier_config.cpp
    latency_matrix.cpp
    dendrogram.cpp
    topology_graph.cpp
    topology_builder.cpp
)

set(TOPOLOGY_HEADERS
    tier_config.hpp
    latency_matrix.hpp
    dendrogram.hpp
    topology_graph.hpp
    topology_builder.hpp
)

add_library(topology STATIC ${TOPOLOGY_SOURCES})
add_library(nixl_topo::topology ALIAS topology)

target_include_directories(topology PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(topology PUBLIC
    nixl_topo::common
    $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
)

# Installation
install(TARGETS topology
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${TOPOLOGY_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nixl_topo/topology
)
