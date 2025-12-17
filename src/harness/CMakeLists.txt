# =============================================================================
# src/harness/CMakeLists.txt
# Harness component - test harness for containerized cluster deployment
# =============================================================================

set(HARNESS_SOURCES
    harness_main.cpp
    harness.cpp
    config/cluster_config.cpp
    network/network_manager.cpp
    tc/tc_manager.cpp
)

add_executable(harness ${HARNESS_SOURCES})
add_executable(nixl_topo::harness ALIAS harness)

target_include_directories(harness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(harness PRIVATE
    nixl_topo::common
    Threads::Threads
    $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
)

# Pass JSON availability to source
if(WITH_JSON)
    target_compile_definitions(harness PRIVATE HAVE_JSON)
endif()

# Installation
install(TARGETS harness
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
