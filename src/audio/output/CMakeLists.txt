target_sources(receiver PRIVATE audio.cpp audio_pa.cpp)
add_library(receiver-output-queue STATIC pcm_output_queue.cpp)
target_include_directories(receiver-output-queue PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(receiver PUBLIC receiver-output-queue)
