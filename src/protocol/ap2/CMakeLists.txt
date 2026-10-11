target_sources(receiver PRIVATE
  ap2_buffered_audio_processor.cpp ap2_event_receiver.cpp ap2_event_message_handler.cpp)
add_library(receiver-buffered-flush STATIC buffered_flush_policy.cpp)
target_include_directories(receiver-buffered-flush PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(receiver-buffered-flush PRIVATE -Wall -Wextra -Wshadow -Wformat=2 -fno-common)
target_link_libraries(receiver-buffered-flush PUBLIC receiver-mod23 Threads::Threads)
