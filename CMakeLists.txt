cmake_minimum_required(VERSION 3.20)
project(fbvector LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On MSVC, remove global /EHsc flag so Google Highway can disable exceptions
# without triggering D9025 command line warning overrides.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
endif()

# Enable position-independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Firebird includes and libraries
find_path(FIREBIRD_INCLUDE_DIR
    NAMES firebird/UdrCppEngine.h ibase.h
    PATHS /opt/firebird /usr/local /usr
    PATH_SUFFIXES include
)

if(NOT FIREBIRD_INCLUDE_DIR)
    message(FATAL_ERROR "Firebird headers not found. Please install Firebird dev package or set FIREBIRD_INCLUDE_DIR.")
else()
    message(STATUS "Found Firebird include directory: ${FIREBIRD_INCLUDE_DIR}")
endif()

find_library(FIREBIRD_CLIENT_LIB
    NAMES fbclient
    PATHS /opt/firebird /usr/local /usr
    PATH_SUFFIXES lib
)

find_library(FIREBIRD_IB_UTIL_LIB
    NAMES ib_util
    PATHS /opt/firebird /usr/local /usr
    PATH_SUFFIXES lib
)

# ==========================================
# Google Highway (Portable SIMD)
# ==========================================
include(FetchContent)
FetchContent_Declare(
    hwy
    URL https://github.com/google/highway/archive/refs/tags/1.2.0.tar.gz
)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(hwy)

# Output directories
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Main Extension Library
add_library(fbvector SHARED
    src/distance.cpp
    src/binary_layout.cpp
    src/udr_entry.cpp
)

set_target_properties(fbvector PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

target_include_directories(fbvector PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${FIREBIRD_INCLUDE_DIR}
)

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fbvector PRIVATE
        -Wall -Wextra -Wpedantic
        -O3 -ffast-math
        -march=native
    )
elseif(MSVC)
    target_compile_options(fbvector PRIVATE
        /W4 /O2 /fp:fast /EHsc
    )
endif()

# Link Firebird libraries if found
if(FIREBIRD_CLIENT_LIB)
    target_link_libraries(fbvector PRIVATE ${FIREBIRD_CLIENT_LIB})
    message(STATUS "Linking with fbclient: ${FIREBIRD_CLIENT_LIB}")
endif()
if(FIREBIRD_IB_UTIL_LIB)
    target_link_libraries(fbvector PRIVATE ${FIREBIRD_IB_UTIL_LIB})
    message(STATUS "Linking with ib_util: ${FIREBIRD_IB_UTIL_LIB}")
endif()

# Link Highway library
target_link_libraries(fbvector PRIVATE hwy)

# Ensure the library name doesn't have "lib" prefix on Windows, but does on Unix
if(WIN32)
    set_target_properties(fbvector PROPERTIES PREFIX "")
endif()

# ==========================================
# Testing (GoogleTest Integration)
# ==========================================
option(BUILD_TESTING "Build the test suite" ON)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(fbvector_tests
        tests/unit/test_distance.cpp
    )

    target_link_libraries(fbvector_tests PRIVATE
        fbvector
        gtest_main
    )

    target_include_directories(fbvector_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Compiler-specific flags for tests
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(fbvector_tests PRIVATE -Wall -Wextra)
    elseif(MSVC)
        target_compile_options(fbvector_tests PRIVATE /EHsc)
    endif()

    include(GoogleTest)
    gtest_discover_tests(fbvector_tests)
endif()

# ==========================================
# Optional External ANN Index Sidecar Daemon
# ==========================================
option(BUILD_SIDECAR "Build the optional External ANN Index Sidecar daemon" OFF)
if(BUILD_SIDECAR AND NOT WIN32)
    add_subdirectory(sidecar)
endif()
