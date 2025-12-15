# =============================================================================
# src/common/CMakeLists.txt
# Common library with shared utilities, types, and protocol definitions
# =============================================================================

set(COMMON_SOURCES
    # Add source files here as they are created
    # nixl_wrapper.cpp
    # protocol.cpp
)

set(COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/nixl_wrapper.hpp
    ${CMAKE_SOURCE_DIR}/include/protocol.hpp
    ${CMAKE_SOURCE_DIR}/include/types.hpp
)

# For now, create an INTERFACE library since we may only have headers
# Change to STATIC once source files are added
add_library(common INTERFACE)
add_library(nixl_topo::common ALIAS common)

target_include_directories(common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(common INTERFACE
    Threads::Threads
    $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
    $<$<BOOL:${WITH_UCX}>:UCX::UCX>
    $<$<BOOL:${WITH_JSON}>:nlohmann_json::nlohmann_json>
)

# When source files are added, uncomment and modify:
# add_library(common STATIC ${COMMON_SOURCES})
# target_sources(common PRIVATE ${COMMON_SOURCES})
# target_include_directories(common PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
#     $<INSTALL_INTERFACE:include>
# )
