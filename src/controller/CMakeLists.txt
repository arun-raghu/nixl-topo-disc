# =============================================================================
# src/controller/CMakeLists.txt
# Controller component - orchestrates agents and collects results
# =============================================================================

set(CONTROLLER_SOURCES
    controller_buffer.cpp
    controller.cpp
    controller_main.cpp
)

add_executable(controller ${CONTROLLER_SOURCES})
add_executable(nixl_topo::controller ALIAS controller)

target_include_directories(controller PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(controller PRIVATE
    nixl_topo::common
    Threads::Threads
    $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
)

# Installation
install(TARGETS controller
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
