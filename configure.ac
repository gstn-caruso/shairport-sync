AC_PREREQ([2.69])
AC_INIT([shairport-sync], [5.5.1], [4265913+mikebrady@users.noreply.github.com])
: ${CFLAGS="-O3"}
AC_CONFIG_SRCDIR([shairport.c])
AC_CONFIG_HEADERS([config.h])

# Reject legacy switches before probing dependencies, including --without forms.
for product_option in $ac_configure_args; do
  product_option=`AS_ECHO(["$product_option"]) | tr -d "'"`
  case "$product_option" in
    --with-dummy*|--without-dummy*|--with-stdout*|--without-stdout*|--with-pipe|--with-pipe=*|--without-pipe|--without-pipe=*|--with-alsa*|--without-alsa*|--with-jack*|--without-jack*|--with-sndio*|--without-sndio*|--with-ao*|--without-ao*|--with-soundio*|--without-soundio*|--with-pipewire*|--without-pipewire*|--with-external-mdns*|--without-external-mdns*|--with-tinysvcmdns*|--without-tinysvcmdns*|--with-dns_sd*|--without-dns_sd*|--with-apple-alac*|--without-apple-alac*|--with-libdaemon*|--without-libdaemon*|--with-piddir*|--without-piddir*|--with-metadata*|--without-metadata*|--with-dbus*|--without-dbus*|--with-mpris*|--without-mpris*|--with-mqtt*|--without-mqtt*|--with-convolution*|--without-convolution*|--with-soxr*|--without-soxr*|--with-systemv*|--without-systemv*|--with-systemd*|--without-systemd*|--with-freebsd*|--without-freebsd*|--with-cygwin*|--without-cygwin*|--with-create-user-group*|--without-create-user-group*|--with-pkg-config*|--without-pkg-config*|--with-pa|--without-pa)
      AC_MSG_ERROR([removed option: $product_option; this fork supports only AirPlay 2 on Linux with PulseAudio, Avahi and OpenSSL]);;
    --with-*|--without-*)
      case "${product_option%%=*}" in
        --with-os|--without-os|--with-airplay-2|--without-airplay-2|--with-pulseaudio|--without-pulseaudio|--with-avahi|--without-avahi|--with-ffmpeg|--without-ffmpeg|--with-ssl|--without-ssl|--with-configfiles|--without-configfiles) ;;
        *) AC_MSG_ERROR([removed option: $product_option; this fork has a fixed receiver stack]);;
      esac;;
  esac
done
AC_ARG_WITH([os], [AS_HELP_STRING([--with-os=linux], [Linux is the only supported platform])], [], [with_os=linux])
AS_IF([test "x$with_os" != xlinux], [AC_MSG_ERROR([Linux is the only supported platform])])
AS_IF([test "`uname -s`" != Linux], [AC_MSG_ERROR([Linux is the only supported platform])])
AC_ARG_WITH([airplay-2], [AS_HELP_STRING([--with-airplay-2], [AirPlay 2 is required])], [], [with_airplay_2=yes])
AC_ARG_WITH([pulseaudio], [AS_HELP_STRING([--with-pulseaudio], [PulseAudio is required])], [], [with_pulseaudio=yes])
AC_ARG_WITH([avahi], [AS_HELP_STRING([--with-avahi], [Avahi is required])], [], [with_avahi=yes])
AC_ARG_WITH([ffmpeg], [AS_HELP_STRING([--with-ffmpeg], [FFmpeg is required])], [], [with_ffmpeg=yes])
AC_ARG_WITH([ssl], [AS_HELP_STRING([--with-ssl=openssl], [OpenSSL is required])], [], [with_ssl=openssl])
AS_IF([test "x$with_airplay_2" != xyes -o "x$with_pulseaudio" != xyes -o "x$with_avahi" != xyes -o "x$with_ffmpeg" != xyes -o "x$with_ssl" != xopenssl], [AC_MSG_ERROR([this fork requires AirPlay 2, PulseAudio, Avahi, FFmpeg and OpenSSL])])
AS_IF([test -z "$CC" -o -z "$CXX"], [
  pinned_cc=`cd "$srcdir" && asdf which clang` || AC_MSG_ERROR([install the pinned asdf toolchain])
  pinned_cxx=`cd "$srcdir" && asdf which clang++` || AC_MSG_ERROR([install the pinned asdf toolchain])
  : ${CC:="$pinned_cc"}
  : ${CXX:="$pinned_cxx"}
])
AC_PROG_CC
AC_PROG_CXX
AC_LANG_PUSH([C])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#if !defined(__clang__) || __clang_major__ != 23 || __clang_minor__ != 1 || __clang_patchlevel__ != 3
#error Clang 23.1.3 is required
#endif
int main(void) { return 0; }
]])], [], [AC_MSG_ERROR([Clang 23.1.3 is required for both compilers])])
AC_LANG_POP([C])
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#if !defined(__clang__) || __clang_major__ != 23 || __clang_minor__ != 1 || __clang_patchlevel__ != 3
#error Clang 23.1.3 is required
#endif
int main() { return 0; }
]])], [], [AC_MSG_ERROR([Clang 23.1.3 is required for both compilers])])
saved_cxxflags="$CXXFLAGS"
saved_cppflags="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -I$srcdir"
CXXFLAGS="$CXXFLAGS -std=c++26 -pthread"
AC_RUN_IFELSE([AC_LANG_SOURCE([[#include "cmake/cpp26_probe.cpp"]])], [],
  [AC_MSG_ERROR([C++26 expected/span/format/jthread with libstdc++ 15 must compile, link and run])],
  [AC_MSG_ERROR([cross-compilation cannot verify the C++26 runtime])])
CXXFLAGS="$saved_cxxflags -std=c++26"
CPPFLAGS="$saved_cppflags"
AC_LANG_POP([C++])
AM_INIT_AUTOMAKE([subdir-objects])
AC_PROG_RANLIB
AC_CHECK_TOOL([AR], [ar], [:])
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG([0.9.0])
AC_ARG_WITH([configfiles], [AS_HELP_STRING([--without-configfiles], [do not install the sample configuration])], [], [with_configfiles=yes])
AM_CONDITIONAL([INSTALL_CONFIG_FILES], [test "x$with_configfiles" = xyes])
AC_DEFINE([CONFIG_AIRPLAY_2], [1], [AirPlay 2 receiver])
AC_DEFINE([CONFIG_PULSEAUDIO], [1], [PulseAudio output])
AC_DEFINE([CONFIG_AVAHI], [1], [Avahi service discovery])
AC_DEFINE([CONFIG_OPENSSL], [1], [OpenSSL cryptography])
AC_DEFINE([CONFIG_FFMPEG], [1], [FFmpeg decoding and resampling])
AC_CHECK_LIB([rt], [clock_gettime], [], [AC_MSG_ERROR([librt required])])
AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR([pthread required])])
AC_CHECK_LIB([m], [exp], [], [AC_MSG_ERROR([libm required])])
PKG_CHECK_MODULES([RECEIVER], [popt libconfig libpulse >= 0.9.2 avahi-client libssl libcrypto libsodium libgcrypt uuid libavutil libavcodec libavformat libswresample], [CFLAGS="$RECEIVER_CFLAGS $CFLAGS" LIBS="$RECEIVER_LIBS $LIBS"])
PKG_CHECK_MODULES([PLIST], [libplist-2.0 >= 2.3.0], [CFLAGS="$PLIST_CFLAGS $CFLAGS" LIBS="$PLIST_LIBS $LIBS" AC_DEFINE([HAVE_LIBPLIST_GE_2_3_0], [1], [libplist >= 2.3.0])], [PKG_CHECK_MODULES([PLIST], [libplist-2.0 >= 2.0.0], [CFLAGS="$PLIST_CFLAGS $CFLAGS" LIBS="$PLIST_LIBS $LIBS"])])
AC_CHECK_PROG([PLISTUTIL], [plistutil], [yes])
CXXFLAGS="$RECEIVER_CFLAGS $PLIST_CFLAGS $CXXFLAGS"
AS_IF([test "x$PLISTUTIL" != xyes], [AC_MSG_ERROR([plistutil required; install libplist-utils])])
AC_CHECK_PROG([XXD], [xxd], [yes])
AS_IF([test "x$XXD" != xyes], [AC_MSG_ERROR([xxd required])])
AC_CHECK_PROGS([GIT], [git])
AS_IF([test -n "$GIT" && test -e "$srcdir/.git/index"], [AC_DEFINE([CONFIG_USE_GIT_VERSION_STRING], [1], [Git version string])])
AM_CONDITIONAL([USE_GIT_VERSION], [test -n "$GIT" && test -e "$srcdir/.git/index"])
AC_CHECK_HEADERS([getopt_long.h arpa/inet.h fcntl.h limits.h memory.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h syslog.h unistd.h])
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_FUNC_ALLOCA
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_CHECK_FUNCS([atexit clock_gettime gethostname inet_ntoa memchr memmove memset pow select socket stpcpy strcasecmp strchr strdup strerror strstr strtol strtoul])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
