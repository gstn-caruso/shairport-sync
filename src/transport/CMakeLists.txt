add_library(receiver-byte-queue STATIC bounded_byte_queue.cpp)
target_include_directories(receiver-byte-queue PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(receiver-byte-queue PRIVATE -Wall -Wextra -Wshadow -Wformat=2 -fno-common)
target_link_libraries(receiver-byte-queue PUBLIC Threads::Threads)
