add_library(receiver-text STATIC service_name_formatter.cpp)
target_include_directories(receiver-text PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_options(receiver-text PRIVATE -Wall -Wextra -Wshadow -Wformat=2)
target_sources(receiver PRIVATE
  debug.cpp network_utilities.cpp rtsp_message_utilities.cpp string_utilities.cpp
  exit.cpp buffered_read.cpp structured_buffer.cpp mod23.cpp
  generate_random_uuid.cpp generate_device_uuid.cpp)
