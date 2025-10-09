cmake_minimum_required(VERSION 3.1...3.27)

project(
  NoCopyRingFifoProj
  VERSION 1.0)
  
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG "origin/main"
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
  
add_subdirectory(no_copy_ring_fifo)
add_subdirectory(tests)

include(GNUInstallDirs)
install(TARGETS NoCopyRingFifo)