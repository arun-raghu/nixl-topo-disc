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

# Unit tests
set(UNIT_TEST_SOURCES
    # Add test files here as they are created
    # test_protocol.cpp
    # test_types.cpp
    # test_topology.cpp
)

# Integration tests
set(INTEGRATION_TEST_SOURCES
    # Add test files here as they are created
    # test_agent_controller.cpp
    # test_nixl_comm.cpp
)

# =============================================================================
# Create Test Executables (placeholder until test sources exist)
# =============================================================================

# For now, create a simple placeholder test
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
# When actual test files are added, uncomment:
# =============================================================================

# # Unit tests executable
# if(UNIT_TEST_SOURCES)
#     add_executable(unit_tests ${UNIT_TEST_SOURCES})
#     target_include_directories(unit_tests PRIVATE
#         ${CMAKE_SOURCE_DIR}/include
#         ${CMAKE_SOURCE_DIR}/src
#     )
#     target_link_libraries(unit_tests PRIVATE
#         GTest::gtest
#         GTest::gtest_main
#         nixl_topo::common
#         $<$<TARGET_EXISTS:nixl_topo::topology>:nixl_topo::topology>
#     )
#     gtest_discover_tests(unit_tests)
# endif()
#
# # Integration tests executable
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
