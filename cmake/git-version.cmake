execute_process(COMMAND "${GIT}" describe --tags --dirty --broken --always
  WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE status ERROR_QUIET)
if(NOT status EQUAL 0)
  set(version "NA")
endif()
set(header "char git_version_string[] = \"${version}\";\n")
set(previous "")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL header)
  file(WRITE "${OUTPUT}" "${header}")
endif()
