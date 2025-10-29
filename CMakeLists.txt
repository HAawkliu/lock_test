cmake_minimum_required(VERSION 3.12)
project(lock_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer pthread flag on platforms that need it
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Enable more warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sources
add_executable(lock_test
  src/main.cpp
  src/lockTestSys.cpp
)

target_include_directories(lock_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

target_link_libraries(lock_test PRIVATE Threads::Threads)

# Optional: manual override of TSC frequency (in GHz) for CycleTimer
# Usage:
#   cmake -S . -B build -DCYCLE_TIMER_TSC_GHZ=3.2
#   cmake --build build
set(CYCLE_TIMER_TSC_GHZ "" CACHE STRING "Override TSC base frequency in GHz for CycleTimer (defines LT_TSC_GHZ)")
if(CYCLE_TIMER_TSC_GHZ)
  target_compile_definitions(lock_test PRIVATE LT_TSC_GHZ=${CYCLE_TIMER_TSC_GHZ})
  message(STATUS "CycleTimer: using manual TSC GHz override: ${CYCLE_TIMER_TSC_GHZ}")
endif()