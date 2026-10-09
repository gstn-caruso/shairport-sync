add_library(receiver-playback-timing STATIC playback_sync.cpp playback_timing.cpp)
target_link_libraries(receiver-playback-timing PUBLIC Threads::Threads)
