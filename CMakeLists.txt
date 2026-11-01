cmake_minimum_required(VERSION 3.16)
project(RiskDB VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization and Vectorization Flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -ffast-math -Wall -Wextra -Wno-unused-parameter)
    # Enable AVX2 if supported by target architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        add_compile_options(-mavx2 -mfma)
    endif()
endif()

# Find OpenMP
find_package(OpenMP)
if(NOT OpenMP_FOUND AND APPLE)
    # Check Homebrew libomp location on macOS
    if(EXISTS "/opt/homebrew/opt/libomp")
        set(OpenMP_CXX_FLAGS -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include)
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")
        set(OpenMP_FOUND TRUE)
        message(STATUS "Configured Homebrew OpenMP from /opt/homebrew/opt/libomp")
    endif()
endif()

# Find PostgreSQL (libpq)
find_path(PostgreSQL_INCLUDE_DIR libpq-fe.h
    PATHS
    /opt/homebrew/opt/libpq/include
    /usr/include/postgresql
    /usr/local/include/postgresql
    /usr/include
)

find_library(PostgreSQL_LIBRARY NAMES pq
    PATHS
    /opt/homebrew/opt/libpq/lib
    /usr/lib/postgresql
    /usr/local/lib
    /usr/lib
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(PostgreSQL_INCLUDE_DIR)
    include_directories(${PostgreSQL_INCLUDE_DIR})
    message(STATUS "Found PostgreSQL headers at: ${PostgreSQL_INCLUDE_DIR}")
endif()

# Core Library Sources
set(RISKDB_CORE_SRCS
    src/common/math_utils.cpp
    src/columnar/columnar_table.cpp
    src/columnar/columnar_pivot.cpp
    src/simd/simd_black_scholes.cpp
    src/engine/query_engine.cpp
    src/storage/data_generator.cpp
    src/storage/postgres_connector.cpp
    src/benchmark/benchmark_runner.cpp
    src/server/http_server.cpp
)

add_library(riskdb_core STATIC ${RISKDB_CORE_SRCS})

if(OpenMP_FOUND)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(riskdb_core PUBLIC OpenMP::OpenMP_CXX)
    else()
        target_include_directories(riskdb_core PUBLIC /opt/homebrew/opt/libomp/include)
        target_compile_options(riskdb_core PUBLIC -Xpreprocessor -fopenmp)
        target_link_libraries(riskdb_core PUBLIC ${OpenMP_omp_LIBRARY})
    endif()
    message(STATUS "OpenMP enabled for parallel engine")
else()
    message(WARNING "OpenMP not found. Engine will run single-threaded.")
endif()

if(PostgreSQL_LIBRARY)
    target_link_libraries(riskdb_core PUBLIC ${PostgreSQL_LIBRARY})
    message(STATUS "Linked libpq: ${PostgreSQL_LIBRARY}")
endif()

# 1. Main RiskDB Engine CLI
add_executable(riskdb src/main.cpp)
target_link_libraries(riskdb PRIVATE riskdb_core)

# 2. VM 1: Traditional Application
add_executable(traditional_app apps/traditional_app/main.cpp)
target_link_libraries(traditional_app PRIVATE riskdb_core)

# 3. VM 2: RiskDB Application
add_executable(riskdb_app apps/riskdb_app/main.cpp)
target_link_libraries(riskdb_app PRIVATE riskdb_core)

# Unit Tests
enable_testing()

add_executable(test_simd_accuracy tests/test_simd_accuracy.cpp)
target_link_libraries(test_simd_accuracy PRIVATE riskdb_core)
add_test(NAME TestSIMDAccuracy COMMAND test_simd_accuracy)

add_executable(test_reduction_tree tests/test_reduction_tree.cpp)
target_link_libraries(test_reduction_tree PRIVATE riskdb_core)
add_test(NAME TestReductionTree COMMAND test_reduction_tree)

add_executable(test_columnar_pivot tests/test_columnar_pivot.cpp)
target_link_libraries(test_columnar_pivot PRIVATE riskdb_core)
add_test(NAME TestColumnarPivot COMMAND test_columnar_pivot)
