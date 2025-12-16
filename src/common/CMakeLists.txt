# =============================================================================
# src/common/CMakeLists.txt
# Common library with shared utilities, types, and NIXL wrapper
# =============================================================================

set(COMMON_SOURCES
    nixl_wrapper.cpp
)

add_library(common STATIC ${COMMON_SOURCES})
add_library(nixl_topo::common ALIAS common)

target_include_directories(common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(common PUBLIC
    $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
)
