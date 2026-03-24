cmake_minimum_required(VERSION 3.10)
project(ColorVision CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimisation flags — required for meaningful throughput benchmarks
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native")

find_package(OpenCV REQUIRED)

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

# ── Shared color science library ─────────────────────────────────────────────
add_library(color_science STATIC
    src/ColorDetector.cpp
    src/ColorScience.cpp
    src/DominantColors.cpp
    src/ColorblindFilter.cpp
)
target_link_libraries(color_science ${OpenCV_LIBS})

# ── color_cli: interactive command-line tool ─────────────────────────────────
add_executable(color_cli src/main.cpp)
target_link_libraries(color_cli color_science ${OpenCV_LIBS})

# ── color_camera: real-time camera analysis ──────────────────────────────────
add_executable(color_camera src/realtime.cpp)
target_link_libraries(color_camera color_science ${OpenCV_LIBS})

# ── color_bench: quantitative evaluation framework ───────────────────────────
add_executable(color_bench src/Benchmark.cpp)
target_link_libraries(color_bench color_science ${OpenCV_LIBS})
