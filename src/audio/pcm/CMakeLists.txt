add_library(receiver-pcm STATIC pcm_encoder.cpp playback_samples.cpp)
target_link_libraries(receiver-pcm PUBLIC PkgConfig::AVUTIL)
