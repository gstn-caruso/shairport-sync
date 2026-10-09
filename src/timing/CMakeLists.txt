add_library(receiver-rtp-clock STATIC rtp_clock.cpp)
target_sources(receiver PRIVATE ptp-utilities.cpp)
