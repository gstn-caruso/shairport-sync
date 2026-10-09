ARFLAGS = cr
AM_CFLAGS = --include=utilities/debug.h -Wshadow -fno-common -Wno-multichar -Wall -Wextra -Wformat=2 -Wno-clobbered -Wno-psabi -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = shairport-sync
AM_CXXFLAGS = -std=c++26 -Wall -Wextra -Wshadow -Wformat=2 -pthread
noinst_LIBRARIES = lib_pair_ap.a lib_receiver.a lib_receiver_text.a
lib_receiver_text_a_SOURCES = utilities/service_name_formatter.cpp
lib_pair_ap_a_CFLAGS = -Wall -g -DCONFIG_GCRYPT -pthread --include=utilities/debug.h
lib_pair_ap_a_SOURCES = pair_ap/pair.c pair_ap/pair_fruit.c pair_ap/pair_homekit.c pair_ap/pair-tlv.c
shairport_sync_LDADD = lib_receiver.a lib_receiver_text.a lib_pair_ap.a
shairport_sync_SOURCES = main.c
shairport_sync_LINK = $(CXXLINK)
lib_receiver_a_SOURCES = shairport.c bonjour_strings.c mdns.c mdns_avahi.c common.c rtp.c \
  player.c audio.c audio_pa.c activity_monitor.c rtsp.c \
  ap2_buffered_audio_processor.c ap2_event_receiver.c ap2_event_message_handler.c ptp-utilities.c \
  utilities/debug.c utilities/network_utilities.c utilities/rtsp_message_utilities.c \
  utilities/string_utilities.cpp utilities/exit.c utilities/buffered_read.c \
  utilities/structured_buffer.cpp utilities/mod23.c utilities/generate_random_uuid.c \
  utilities/generate_device_uuid.c plists/get_info_response.c
BUILT_SOURCES = plists/get_info_response.h plists/get_info_response.c
CLEANFILES = plists/get_info_response.h plists/get_info_response.c
plists/get_info_response.h: $(top_srcdir)/plists/get_info_response.xml
	$(MKDIR_P) plists
	sh $(top_srcdir)/xml_plist_codegen.sh $(top_srcdir)/plists/get_info_response.xml $(abs_builddir)/plists
plists/get_info_response.c: plists/get_info_response.h
	touch plists/get_info_response.c
if USE_GIT_VERSION
.PHONY: gitversion-check
gitversion-check:
	cd $(top_srcdir) && ./verify-gitversion
BUILT_SOURCES += gitversion-check
endif
man_MANS = man/shairport-sync.1
EXTRA_DIST = scripts/shairport-sync.conf scripts/shairport-sync.user.service \
  plists/get_info_response.xml xml_plist_codegen.sh tests/configure_test.sh \
  tests/nqptp_test.sh tests/nqptp_fixture.c tests/removed_options_test.sh \
  verify-gitversion user-service-install.sh README.md BUILD.md AIRPLAY2.md \
  ADDINGTOHOME.md TROUBLESHOOTING.md CONFIGURATION.md VALIDATION.md
EXTRA_DIST += CMakeLists.txt .tool-versions cmake/clang-toolchain.cmake \
  cmake/config.h.in cmake/cpp26_probe.cpp cmake/git-version.cmake \
  tests/cmake_configure_test.sh tests/receiver_linkage_test.cpp
TESTS = tests/configure_test.sh tests/nqptp_test.sh tests/removed_options_test.sh
if INSTALL_CONFIG_FILES
install-exec-hook:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)
	$(INSTALL_DATA) $(top_srcdir)/scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf.sample
endif
check_PROGRAMS = rtsp-dispatch-test structured-buffer-test structured-buffer-cpp-test string-utilities-test string-utilities-cpp-test
string_utilities_cpp_test_SOURCES = tests/string_utilities_cpp_test.cpp
string_utilities_cpp_test_CXXFLAGS = $(AM_CXXFLAGS) -UNDEBUG
string_utilities_cpp_test_LDADD = lib_receiver_text.a
string_utilities_test_SOURCES = tests/string_utilities_test.c
string_utilities_test_CFLAGS = $(AM_CFLAGS) -UNDEBUG
string_utilities_test_LDADD = $(shairport_sync_LDADD)
string_utilities_test_LINK = $(CXXLINK)
structured_buffer_cpp_test_SOURCES = tests/structured_buffer_cpp_test.cpp
structured_buffer_cpp_test_CXXFLAGS = $(AM_CXXFLAGS) -UNDEBUG
structured_buffer_cpp_test_LDADD = $(shairport_sync_LDADD)
structured_buffer_test_SOURCES = tests/structured_buffer_test.c
structured_buffer_test_CFLAGS = $(AM_CFLAGS) -UNDEBUG
structured_buffer_test_LDADD = $(shairport_sync_LDADD)
structured_buffer_test_LINK = $(CXXLINK)
rtsp_dispatch_test_SOURCES = tests/rtsp_dispatch_test.c
rtsp_dispatch_test_CFLAGS = $(AM_CFLAGS) -UNDEBUG
rtsp_dispatch_test_LDADD = $(shairport_sync_LDADD)
rtsp_dispatch_test_LINK = $(CXXLINK)
TESTS += rtsp-dispatch-test
TESTS += structured-buffer-test structured-buffer-cpp-test
TESTS += string-utilities-test string-utilities-cpp-test
noinst_HEADERS = receiver.h activity_monitor.h ap2_buffered_audio_processor.h ap2_event_message_handler.h \
  ap2_event_receiver.h audio.h bonjour_strings.h common.h definitions.h mdns.h \
  nqptp-shm-structures.h player.h ptp-utilities.h rtp.h rtsp.h \
  pair_ap/pair.h pair_ap/pair-internal.h pair_ap/pair-tlv.h \
  utilities/buffered_read.h utilities/debug.h utilities/exit.h \
  utilities/generate_device_uuid.h utilities/generate_random_uuid.h utilities/mod23.h \
  utilities/network_utilities.h utilities/rtsp_message_utilities.h \
  utilities/string_utilities.h utilities/string_utilities.hpp utilities/structured_buffer.h utilities/structured_buffer.hpp
