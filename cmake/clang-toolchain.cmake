find_program(SHAIRPORT_ASDF asdf REQUIRED)
foreach(language C CXX)
  if(language STREQUAL "C")
    set(compiler clang)
  else()
    set(compiler clang++)
  endif()
  execute_process(COMMAND "${SHAIRPORT_ASDF}" which "${compiler}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
    OUTPUT_VARIABLE compiler_path OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE compiler_status ERROR_VARIABLE compiler_error)
  if(NOT compiler_status EQUAL 0)
    message(FATAL_ERROR "Install the pinned asdf toolchain: ${compiler_error}")
  endif()
  set(CMAKE_${language}_COMPILER "${compiler_path}" CACHE FILEPATH "")
endforeach()
