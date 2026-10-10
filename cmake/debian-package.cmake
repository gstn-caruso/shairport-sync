set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME shairport-sync)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Shairport Sync maintainers <shairport-sync@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AirPlay 2 audio receiver for PulseAudio")
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_PACKAGE_CHECKSUM SHA256)

install(FILES COPYING DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync" RENAME copyright)
install(FILES BUILD.md CONFIGURATION.md DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync")
install(FILES LICENSES DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/shairport-sync")
file(READ "${CMAKE_SOURCE_DIR}/scripts/shairport-sync.user.service" packaged_user_service)
string(REPLACE "/usr/local/bin/shairport-sync" "${CMAKE_INSTALL_FULL_BINDIR}/shairport-sync"
  packaged_user_service "${packaged_user_service}")
file(WRITE "${CMAKE_BINARY_DIR}/shairport-sync.service" "${packaged_user_service}")
install(FILES "${CMAKE_BINARY_DIR}/shairport-sync.service"
  DESTINATION "lib/systemd/user")

include(CPack)
