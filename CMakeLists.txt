cmake_minimum_required(VERSION 3.18)
project(nixl-topo-disc
    VERSION 0.1.0
    DESCRIPTION "Cluster Topology Discovery & Performance Measurement System"
    LANGUAGES CXX
)

# =============================================================================
# C++ Standard Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Build Type (default to Release if not specified)
# =============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# =============================================================================
# Output Directories
# =============================================================================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================================
# Build Options - Subprojects
# =============================================================================
option(BUILD_AGENT      "Build the agent component"       ON)
option(BUILD_CONTROLLER "Build the controller component"  ON)
option(BUILD_TOPOLOGY   "Build the topology component"    ON)
option(BUILD_HARNESS    "Build the harness component"     ON)
option(BUILD_TESTS      "Build test suite"                ON)

# =============================================================================
# Build Options - Dependencies
# =============================================================================
option(BUILD_DEPS_FROM_SOURCE "Build dependencies from submodules" ON)
option(WITH_NIXL   "Enable NIXL support"    ON)
option(WITH_UCX    "Enable UCX support"     ON)
option(WITH_GDS    "Enable GDS support"     OFF)
option(WITH_GDR    "Enable GDRCopy support" OFF)

# Paths to pre-installed dependencies (used when BUILD_DEPS_FROM_SOURCE=OFF)
set(NIXL_ROOT "" CACHE PATH "Path to pre-installed NIXL")
set(UCX_ROOT  "" CACHE PATH "Path to pre-installed UCX")
set(GDS_ROOT  "" CACHE PATH "Path to pre-installed GDS")
set(GDR_ROOT  "" CACHE PATH "Path to pre-installed GDRCopy")

# =============================================================================
# Build Options - Features
# =============================================================================
option(ENABLE_CUDA      "Enable CUDA support"               OFF)
option(ENABLE_SANITIZERS "Enable address/undefined sanitizers" OFF)

# =============================================================================
# Compiler Flags
# =============================================================================
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O3>
)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# =============================================================================
# CUDA Configuration (if enabled)
# =============================================================================
if(ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    add_compile_definitions(HAVE_CUDA)
endif()

# =============================================================================
# CMake Module Path
# =============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# =============================================================================
# Dependencies
# =============================================================================
include(cmake/Dependencies.cmake)

# =============================================================================
# Common Library (always built)
# =============================================================================
add_subdirectory(src/common)

# =============================================================================
# Subprojects (conditional)
# =============================================================================
if(BUILD_AGENT)
    add_subdirectory(src/agent)
endif()

if(BUILD_CONTROLLER)
    add_subdirectory(src/controller)
endif()

if(BUILD_TOPOLOGY)
    add_subdirectory(src/topology)
endif()

if(BUILD_HARNESS)
    add_subdirectory(src/harness)
endif()

# =============================================================================
# Tests
# =============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "=== nixl-topo-disc Configuration ===")
message(STATUS "")
message(STATUS "Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix:      ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Subprojects:")
message(STATUS "  BUILD_AGENT:       ${BUILD_AGENT}")
message(STATUS "  BUILD_CONTROLLER:  ${BUILD_CONTROLLER}")
message(STATUS "  BUILD_TOPOLOGY:    ${BUILD_TOPOLOGY}")
message(STATUS "  BUILD_HARNESS:     ${BUILD_HARNESS}")
message(STATUS "  BUILD_TESTS:       ${BUILD_TESTS}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  BUILD_DEPS_FROM_SOURCE: ${BUILD_DEPS_FROM_SOURCE}")
message(STATUS "  WITH_NIXL:         ${WITH_NIXL}")
message(STATUS "  WITH_UCX:          ${WITH_UCX}")
message(STATUS "  WITH_GDS:          ${WITH_GDS}")
message(STATUS "  WITH_GDR:          ${WITH_GDR}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  ENABLE_CUDA:       ${ENABLE_CUDA}")
message(STATUS "  ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")
message(STATUS "")
