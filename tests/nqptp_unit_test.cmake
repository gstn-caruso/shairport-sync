find_program(SYSTEMD_ANALYZE systemd-analyze REQUIRED)
file(READ "${ROOT}${PREFIX}/lib/systemd/system/shairport-sync-nqptp.service" unit)
string(REPLACE "ExecStart=${PREFIX}/" "ExecStart=${ROOT}${PREFIX}/" checked_unit "${unit}")
file(MAKE_DIRECTORY "${ROOT}/unit-check")
file(WRITE "${ROOT}/unit-check/shairport-sync-nqptp.service" "${checked_unit}")
execute_process(COMMAND "${SYSTEMD_ANALYZE}" verify --man=no
  "${ROOT}/unit-check/shairport-sync-nqptp.service"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Systemd rejected the companion unit: ${output} ${error}")
endif()
