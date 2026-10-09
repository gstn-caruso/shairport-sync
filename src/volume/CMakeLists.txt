add_library(receiver-volume-policy STATIC volume_policy.cpp)
target_sources(receiver PRIVATE volume_runtime.cpp)
