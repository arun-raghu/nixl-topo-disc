# =============================================================================
# Dependencies.cmake
# Handles building dependencies from submodules or finding pre-installed ones
# =============================================================================

include(ExternalProject)
include(FetchContent)

# =============================================================================
# Check for required build tools when building from source
# =============================================================================
if(BUILD_DEPS_FROM_SOURCE)
    # UCX requires autotools
    if(WITH_UCX)
        find_program(AUTORECONF autoreconf)
        find_program(AUTOMAKE automake)
        find_program(LIBTOOLIZE NAMES libtoolize glibtoolize libtool)

        if(NOT AUTORECONF)
            message(FATAL_ERROR "autoreconf not found. Install autoconf:\n"
                "  Ubuntu/Debian: sudo apt-get install autoconf\n"
                "  RHEL/CentOS:   sudo yum install autoconf\n"
                "  macOS:         brew install autoconf")
        endif()
        if(NOT AUTOMAKE)
            message(FATAL_ERROR "automake not found. Install automake:\n"
                "  Ubuntu/Debian: sudo apt-get install automake\n"
                "  RHEL/CentOS:   sudo yum install automake\n"
                "  macOS:         brew install automake")
        endif()
        if(NOT LIBTOOLIZE)
            message(FATAL_ERROR "libtoolize not found. Install libtool:\n"
                "  Ubuntu/Debian: sudo apt-get install libtool\n"
                "  RHEL/CentOS:   sudo yum install libtool\n"
                "  macOS:         brew install libtool")
        endif()
    endif()

    # NIXL requires meson and ninja
    if(WITH_NIXL)
        find_program(MESON meson)
        find_program(NINJA ninja)

        if(NOT MESON)
            message(FATAL_ERROR "meson not found. Install meson:\n"
                "  Ubuntu/Debian: sudo apt-get install meson\n"
                "  RHEL/CentOS:   sudo yum install meson\n"
                "  pip:           pip install meson\n"
                "  macOS:         brew install meson")
        endif()
        if(NOT NINJA)
            message(FATAL_ERROR "ninja not found. Install ninja:\n"
                "  Ubuntu/Debian: sudo apt-get install ninja-build\n"
                "  RHEL/CentOS:   sudo yum install ninja-build\n"
                "  pip:           pip install ninja\n"
                "  macOS:         brew install ninja")
        endif()
    endif()

    message(STATUS "Build tools found for building dependencies from source")
endif()

# Directory where built dependencies will be installed
set(DEPS_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps_install")
file(MAKE_DIRECTORY ${DEPS_INSTALL_DIR})

# =============================================================================
# Helper function to ensure submodule is initialized
# =============================================================================
function(ensure_submodule_initialized submodule_path)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${submodule_path}/.git")
        message(STATUS "Initializing submodule: ${submodule_path}")
        execute_process(
            COMMAND git submodule update --init --recursive ${submodule_path}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_RESULT
        )
        if(NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to initialize submodule: ${submodule_path}")
        endif()
    endif()
endfunction()

# =============================================================================
# UCX - Unified Communication X
# =============================================================================
if(WITH_UCX)
    if(BUILD_DEPS_FROM_SOURCE)
        ensure_submodule_initialized("submodules/ucx")

        set(UCX_SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/ucx")
        set(UCX_INSTALL_DIR "${DEPS_INSTALL_DIR}/ucx")

        # UCX uses autotools, need to run autogen.sh first
        # BUILD_IN_SOURCE TRUE because autogen.sh must run in source directory
        ExternalProject_Add(ucx_external
            SOURCE_DIR ${UCX_SOURCE_DIR}
            CONFIGURE_COMMAND ${UCX_SOURCE_DIR}/autogen.sh
                COMMAND ${UCX_SOURCE_DIR}/configure
                    --prefix=${UCX_INSTALL_DIR}
                    --enable-mt
                    --disable-logging
                    --disable-debug
                    --disable-assertions
            BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
            INSTALL_COMMAND make install
            BUILD_IN_SOURCE TRUE
            BUILD_BYPRODUCTS
                ${UCX_INSTALL_DIR}/lib/libucp.so
                ${UCX_INSTALL_DIR}/lib/libuct.so
                ${UCX_INSTALL_DIR}/lib/libucs.so
        )

        # Create directories so CMake validation passes
        file(MAKE_DIRECTORY ${UCX_INSTALL_DIR}/include)

        # Create imported targets
        add_library(UCX::ucp SHARED IMPORTED GLOBAL)
        add_library(UCX::uct SHARED IMPORTED GLOBAL)
        add_library(UCX::ucs SHARED IMPORTED GLOBAL)

        set_target_properties(UCX::ucp PROPERTIES
            IMPORTED_LOCATION ${UCX_INSTALL_DIR}/lib/libucp.so
            INTERFACE_INCLUDE_DIRECTORIES ${UCX_INSTALL_DIR}/include
        )
        set_target_properties(UCX::uct PROPERTIES
            IMPORTED_LOCATION ${UCX_INSTALL_DIR}/lib/libuct.so
            INTERFACE_INCLUDE_DIRECTORIES ${UCX_INSTALL_DIR}/include
        )
        set_target_properties(UCX::ucs PROPERTIES
            IMPORTED_LOCATION ${UCX_INSTALL_DIR}/lib/libucs.so
            INTERFACE_INCLUDE_DIRECTORIES ${UCX_INSTALL_DIR}/include
        )

        add_dependencies(UCX::ucp ucx_external)
        add_dependencies(UCX::uct ucx_external)
        add_dependencies(UCX::ucs ucx_external)

        # Convenience target
        add_library(UCX::UCX INTERFACE IMPORTED GLOBAL)
        target_link_libraries(UCX::UCX INTERFACE UCX::ucp UCX::uct UCX::ucs)

        set(UCX_FOUND TRUE)
        set(UCX_INCLUDE_DIRS ${UCX_INSTALL_DIR}/include)
        set(UCX_LIBRARIES ${UCX_INSTALL_DIR}/lib/libucp.so)

    else()
        # Find pre-installed UCX
        if(UCX_ROOT)
            set(CMAKE_PREFIX_PATH ${UCX_ROOT} ${CMAKE_PREFIX_PATH})
        endif()

        find_path(UCX_INCLUDE_DIR ucp/api/ucp.h
            HINTS ${UCX_ROOT}/include ENV UCX_DIR
            PATH_SUFFIXES include
        )
        find_library(UCX_UCP_LIBRARY NAMES ucp
            HINTS ${UCX_ROOT}/lib ENV UCX_DIR
            PATH_SUFFIXES lib lib64
        )
        find_library(UCX_UCT_LIBRARY NAMES uct
            HINTS ${UCX_ROOT}/lib ENV UCX_DIR
            PATH_SUFFIXES lib lib64
        )
        find_library(UCX_UCS_LIBRARY NAMES ucs
            HINTS ${UCX_ROOT}/lib ENV UCX_DIR
            PATH_SUFFIXES lib lib64
        )

        if(UCX_INCLUDE_DIR AND UCX_UCP_LIBRARY)
            set(UCX_FOUND TRUE)
            set(UCX_INCLUDE_DIRS ${UCX_INCLUDE_DIR})
            set(UCX_LIBRARIES ${UCX_UCP_LIBRARY} ${UCX_UCT_LIBRARY} ${UCX_UCS_LIBRARY})

            add_library(UCX::ucp SHARED IMPORTED GLOBAL)
            set_target_properties(UCX::ucp PROPERTIES
                IMPORTED_LOCATION ${UCX_UCP_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${UCX_INCLUDE_DIR}
            )
            add_library(UCX::uct SHARED IMPORTED GLOBAL)
            set_target_properties(UCX::uct PROPERTIES
                IMPORTED_LOCATION ${UCX_UCT_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${UCX_INCLUDE_DIR}
            )
            add_library(UCX::ucs SHARED IMPORTED GLOBAL)
            set_target_properties(UCX::ucs PROPERTIES
                IMPORTED_LOCATION ${UCX_UCS_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${UCX_INCLUDE_DIR}
            )

            add_library(UCX::UCX INTERFACE IMPORTED GLOBAL)
            target_link_libraries(UCX::UCX INTERFACE UCX::ucp UCX::uct UCX::ucs)

            message(STATUS "Found UCX: ${UCX_UCP_LIBRARY}")
        else()
            message(FATAL_ERROR "UCX not found. Set UCX_ROOT or enable BUILD_DEPS_FROM_SOURCE")
        endif()
    endif()

    add_compile_definitions(HAVE_UCX)
endif()

# =============================================================================
# NIXL - NVIDIA Interconnect eXchange Library
# =============================================================================
if(WITH_NIXL)
    if(BUILD_DEPS_FROM_SOURCE)
        ensure_submodule_initialized("submodules/nixl")

        set(NIXL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/nixl")
        set(NIXL_INSTALL_DIR "${DEPS_INSTALL_DIR}/nixl")
        set(NIXL_BUILD_DIR "${CMAKE_BINARY_DIR}/nixl_build")

        # NIXL uses meson build system
        set(NIXL_DEPENDS "")
        if(WITH_UCX)
            set(NIXL_DEPENDS ucx_external)
        endif()

        # Detect lib directory suffix (meson uses lib/x86_64-linux-gnu on some systems)
        set(NIXL_LIB_SUFFIX "lib/x86_64-linux-gnu")

        ExternalProject_Add(nixl_external
            SOURCE_DIR ${NIXL_SOURCE_DIR}
            CONFIGURE_COMMAND meson setup ${NIXL_BUILD_DIR} ${NIXL_SOURCE_DIR}
                --prefix=${NIXL_INSTALL_DIR}
                --buildtype=release
                -Ducx_path=${DEPS_INSTALL_DIR}/ucx
            BUILD_COMMAND meson compile -C ${NIXL_BUILD_DIR}
            INSTALL_COMMAND meson install -C ${NIXL_BUILD_DIR}
            BUILD_IN_SOURCE FALSE
            BUILD_BYPRODUCTS ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX}/libnixl.so
            DEPENDS ${NIXL_DEPENDS}
        )

        # Create directories so CMake validation passes
        file(MAKE_DIRECTORY ${NIXL_INSTALL_DIR}/include)
        file(MAKE_DIRECTORY ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX})

        # Create imported targets for NIXL component libraries
        add_library(NIXL::nixl_build SHARED IMPORTED GLOBAL)
        set_target_properties(NIXL::nixl_build PROPERTIES
            IMPORTED_LOCATION ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX}/libnixl_build.so
        )
        add_dependencies(NIXL::nixl_build nixl_external)

        add_library(NIXL::nixl_common SHARED IMPORTED GLOBAL)
        set_target_properties(NIXL::nixl_common PROPERTIES
            IMPORTED_LOCATION ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX}/libnixl_common.so
        )
        add_dependencies(NIXL::nixl_common nixl_external)

        # Main NIXL library with dependencies
        add_library(NIXL::nixl SHARED IMPORTED GLOBAL)
        set_target_properties(NIXL::nixl PROPERTIES
            IMPORTED_LOCATION ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX}/libnixl.so
            INTERFACE_INCLUDE_DIRECTORIES ${NIXL_INSTALL_DIR}/include
            INTERFACE_LINK_LIBRARIES "NIXL::nixl_build;NIXL::nixl_common"
        )
        add_dependencies(NIXL::nixl nixl_external)

        set(NIXL_FOUND TRUE)
        set(NIXL_INCLUDE_DIRS ${NIXL_INSTALL_DIR}/include)
        set(NIXL_LIBRARIES ${NIXL_INSTALL_DIR}/${NIXL_LIB_SUFFIX}/libnixl.so)

    else()
        # Find pre-installed NIXL
        if(NIXL_ROOT)
            set(CMAKE_PREFIX_PATH ${NIXL_ROOT} ${CMAKE_PREFIX_PATH})
        endif()

        find_path(NIXL_INCLUDE_DIR nixl.h
            HINTS ${NIXL_ROOT}/include ENV NIXL_DIR
            PATH_SUFFIXES include
        )
        find_library(NIXL_LIBRARY NAMES nixl
            HINTS ${NIXL_ROOT}/lib ENV NIXL_DIR
            PATH_SUFFIXES lib lib64
        )

        if(NIXL_INCLUDE_DIR AND NIXL_LIBRARY)
            set(NIXL_FOUND TRUE)
            set(NIXL_INCLUDE_DIRS ${NIXL_INCLUDE_DIR})
            set(NIXL_LIBRARIES ${NIXL_LIBRARY})

            add_library(NIXL::nixl SHARED IMPORTED GLOBAL)
            set_target_properties(NIXL::nixl PROPERTIES
                IMPORTED_LOCATION ${NIXL_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${NIXL_INCLUDE_DIR}
            )

            message(STATUS "Found NIXL: ${NIXL_LIBRARY}")
        else()
            message(FATAL_ERROR "NIXL not found. Set NIXL_ROOT or enable BUILD_DEPS_FROM_SOURCE")
        endif()
    endif()

    add_compile_definitions(HAVE_NIXL)
endif()

# =============================================================================
# GDRCopy - GPU Direct RDMA Copy Library
# =============================================================================
if(WITH_GDR)
    if(BUILD_DEPS_FROM_SOURCE)
        ensure_submodule_initialized("submodules/gdr")

        set(GDR_SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/gdr")
        set(GDR_INSTALL_DIR "${DEPS_INSTALL_DIR}/gdr")

        # GDRCopy uses Makefile
        ExternalProject_Add(gdr_external
            SOURCE_DIR ${GDR_SOURCE_DIR}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND make -C ${GDR_SOURCE_DIR} lib lib_install
                PREFIX=${GDR_INSTALL_DIR}
                DESTLIB=${GDR_INSTALL_DIR}/lib
                DESTINC=${GDR_INSTALL_DIR}/include
                $<$<BOOL:${ENABLE_CUDA}>:CUDA=${CUDAToolkit_LIBRARY_DIR}/..>
            INSTALL_COMMAND ""
            BUILD_IN_SOURCE TRUE
            BUILD_BYPRODUCTS ${GDR_INSTALL_DIR}/lib/libgdrapi.so
        )

        # Create imported target
        add_library(GDR::gdrapi SHARED IMPORTED GLOBAL)
        set_target_properties(GDR::gdrapi PROPERTIES
            IMPORTED_LOCATION ${GDR_INSTALL_DIR}/lib/libgdrapi.so
            INTERFACE_INCLUDE_DIRECTORIES ${GDR_INSTALL_DIR}/include
        )
        add_dependencies(GDR::gdrapi gdr_external)

        set(GDR_FOUND TRUE)
        set(GDR_INCLUDE_DIRS ${GDR_INSTALL_DIR}/include)
        set(GDR_LIBRARIES ${GDR_INSTALL_DIR}/lib/libgdrapi.so)

    else()
        # Find pre-installed GDRCopy
        if(GDR_ROOT)
            set(CMAKE_PREFIX_PATH ${GDR_ROOT} ${CMAKE_PREFIX_PATH})
        endif()

        find_path(GDR_INCLUDE_DIR gdrapi.h
            HINTS ${GDR_ROOT}/include ENV GDRCOPY_HOME
            PATH_SUFFIXES include
        )
        find_library(GDR_LIBRARY NAMES gdrapi
            HINTS ${GDR_ROOT}/lib ENV GDRCOPY_HOME
            PATH_SUFFIXES lib lib64
        )

        if(GDR_INCLUDE_DIR AND GDR_LIBRARY)
            set(GDR_FOUND TRUE)
            set(GDR_INCLUDE_DIRS ${GDR_INCLUDE_DIR})
            set(GDR_LIBRARIES ${GDR_LIBRARY})

            add_library(GDR::gdrapi SHARED IMPORTED GLOBAL)
            set_target_properties(GDR::gdrapi PROPERTIES
                IMPORTED_LOCATION ${GDR_LIBRARY}
                INTERFACE_INCLUDE_DIRECTORIES ${GDR_INCLUDE_DIR}
            )

            message(STATUS "Found GDRCopy: ${GDR_LIBRARY}")
        else()
            message(FATAL_ERROR "GDRCopy not found. Set GDR_ROOT or enable BUILD_DEPS_FROM_SOURCE")
        endif()
    endif()

    add_compile_definitions(HAVE_GDR)
endif()

# =============================================================================
# GDS - GPU Direct Storage (nvidia-fs kernel module)
# =============================================================================
if(WITH_GDS)
    if(BUILD_DEPS_FROM_SOURCE)
        ensure_submodule_initialized("submodules/gds")

        # GDS is primarily a kernel module; we only need headers for user-space
        set(GDS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/gds")
        set(GDS_INCLUDE_DIR "${GDS_SOURCE_DIR}/src")

        # Create header-only imported target
        add_library(GDS::gds INTERFACE IMPORTED GLOBAL)
        set_target_properties(GDS::gds PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${GDS_INCLUDE_DIR}
        )

        set(GDS_FOUND TRUE)
        set(GDS_INCLUDE_DIRS ${GDS_INCLUDE_DIR})

        message(STATUS "Using GDS headers from: ${GDS_INCLUDE_DIR}")

    else()
        # Find pre-installed GDS (cufile library from CUDA toolkit)
        if(GDS_ROOT)
            set(CMAKE_PREFIX_PATH ${GDS_ROOT} ${CMAKE_PREFIX_PATH})
        endif()

        find_path(GDS_INCLUDE_DIR cufile.h
            HINTS ${GDS_ROOT}/include ${CUDAToolkit_INCLUDE_DIRS}
            PATH_SUFFIXES include
        )
        find_library(GDS_LIBRARY NAMES cufile
            HINTS ${GDS_ROOT}/lib ${CUDAToolkit_LIBRARY_DIR}
            PATH_SUFFIXES lib lib64
        )

        if(GDS_INCLUDE_DIR)
            set(GDS_FOUND TRUE)
            set(GDS_INCLUDE_DIRS ${GDS_INCLUDE_DIR})

            add_library(GDS::gds INTERFACE IMPORTED GLOBAL)
            target_include_directories(GDS::gds INTERFACE ${GDS_INCLUDE_DIR})

            if(GDS_LIBRARY)
                set(GDS_LIBRARIES ${GDS_LIBRARY})
                target_link_libraries(GDS::gds INTERFACE ${GDS_LIBRARY})
            endif()

            message(STATUS "Found GDS: ${GDS_INCLUDE_DIR}")
        else()
            message(FATAL_ERROR "GDS not found. Set GDS_ROOT or enable BUILD_DEPS_FROM_SOURCE")
        endif()
    endif()

    add_compile_definitions(HAVE_GDS)
endif()

# =============================================================================
# nlohmann/json - Header-only JSON library (fetched via FetchContent)
# =============================================================================
option(WITH_JSON "Enable JSON support via nlohmann/json" ON)

if(WITH_JSON)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)

    add_compile_definitions(HAVE_JSON)
    message(STATUS "Using nlohmann/json")
endif()

# =============================================================================
# pthread (required for multi-threading)
# =============================================================================
find_package(Threads REQUIRED)

# =============================================================================
# Summary of found dependencies
# =============================================================================
message(STATUS "")
message(STATUS "=== Dependency Status ===")
if(WITH_UCX)
    message(STATUS "  UCX:     ${UCX_FOUND} (${UCX_LIBRARIES})")
endif()
if(WITH_NIXL)
    message(STATUS "  NIXL:    ${NIXL_FOUND} (${NIXL_LIBRARIES})")
endif()
if(WITH_GDR)
    message(STATUS "  GDRCopy: ${GDR_FOUND} (${GDR_LIBRARIES})")
endif()
if(WITH_GDS)
    message(STATUS "  GDS:     ${GDS_FOUND} (${GDS_INCLUDE_DIRS})")
endif()
if(WITH_JSON)
    message(STATUS "  JSON:    Available (nlohmann/json)")
endif()
message(STATUS "")
