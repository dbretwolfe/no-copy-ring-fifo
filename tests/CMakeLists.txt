add_executable(NoCopyRingFifoTest)
target_sources(NoCopyRingFifoTest PUBLIC 
  fifo_test.cpp
)

target_include_directories(NoCopyRingFifoTest PUBLIC ./ ../../no_copy_ring_fifo/)
target_link_libraries(NoCopyRingFifoTest PUBLIC NoCopyRingFifo PRIVATE GTest::gtest_main)
target_compile_features(NoCopyRingFifoTest PUBLIC cxx_std_23)

include(GoogleTest)
gtest_discover_tests(NoCopyRingFifoTest)
