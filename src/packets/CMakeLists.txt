add_library(receiver-packets STATIC retransmission_planner.cpp)
target_include_directories(receiver-packets PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(receiver-packets PRIVATE -Wall -Wextra -Wshadow -Wformat=2)
