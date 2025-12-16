# =============================================================================
# tests/CMakeLists.txt
# Test suite using Google Test
# =============================================================================

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

# Prevent GoogleTest from overriding compiler/linker options
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# =============================================================================
# Test Sources
# =============================================================================

# Placeholder test
add_executable(test_placeholder
    ${CMAKE_CURRENT_SOURCE_DIR}/test_placeholder.cpp
)

target_link_libraries(test_placeholder PRIVATE
    GTest::gtest
    GTest::gtest_main
    nixl_topo::common
)

gtest_discover_tests(test_placeholder)

# =============================================================================
# Types Tests (wire format, BufferHeader, AgentSlot, Notification)
# =============================================================================

add_executable(test_types
    ${CMAKE_CURRENT_SOURCE_DIR}/test_types.cpp
)

target_include_directories(test_types PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_types PRIVATE
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(test_types)

# =============================================================================
# Memory Tests (alloc_buffer, free_buffer)
# =============================================================================

add_executable(test_memory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_memory.cpp
)

target_include_directories(test_memory PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_memory PRIVATE
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(test_memory)

# =============================================================================
# Controller Buffer Tests
# =============================================================================

add_executable(test_controller_buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/test_controller_buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/controller/controller_buffer.cpp
)

target_include_directories(test_controller_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_controller_buffer PRIVATE
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(test_controller_buffer)

# =============================================================================
# Topology Tests
# =============================================================================

add_executable(test_topology
    ${CMAKE_CURRENT_SOURCE_DIR}/test_topology.cpp
)

target_include_directories(test_topology PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_topology PRIVATE
    GTest::gtest
    GTest::gtest_main
    nixl_topo::common
    nixl_topo::topology
)

gtest_discover_tests(test_topology)

# Copy test data to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_data
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# =============================================================================
# Integration tests (placeholder for future)
# =============================================================================

# set(INTEGRATION_TEST_SOURCES
#     # Add test files here as they are created
#     # test_agent_controller.cpp
#     # test_nixl_comm.cpp
# )
#
# if(INTEGRATION_TEST_SOURCES)
#     add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
#     target_include_directories(integration_tests PRIVATE
#         ${CMAKE_SOURCE_DIR}/include
#         ${CMAKE_SOURCE_DIR}/src
#     )
#     target_link_libraries(integration_tests PRIVATE
#         GTest::gtest
#         GTest::gtest_main
#         nixl_topo::common
#         $<$<BOOL:${WITH_NIXL}>:NIXL::nixl>
#         $<$<BOOL:${WITH_UCX}>:UCX::UCX>
#     )
#     gtest_discover_tests(integration_tests)
# endif()
