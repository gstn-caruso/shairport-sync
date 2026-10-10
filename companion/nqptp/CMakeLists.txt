add_library(nqptp-core STATIC
  nqptp.cpp nqptp-clock-sources.cpp nqptp-message-handlers.cpp
  nqptp-utilities.cpp general-utilities.cpp debug.cpp)
target_include_directories(nqptp-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nqptp-core PUBLIC nqptp-ipc Threads::Threads rt)
target_compile_definitions(nqptp-core PRIVATE CONFIG_FOR_LINUX "VERSION=\"${PROJECT_VERSION}\"")
target_compile_options(nqptp-core PRIVATE -Wall -Wextra -pedantic-errors)
add_executable(shairport-sync-nqptp main.cpp)
target_compile_options(shairport-sync-nqptp PRIVATE -Wall -Wextra -pedantic-errors)
target_link_libraries(shairport-sync-nqptp PRIVATE nqptp-core)
install(TARGETS shairport-sync-nqptp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
