/*
 * Shairport, an Apple Airplay receiver
 * Copyright (c) James Laird 2013
 * All rights reserved.
 * Modifications and additions (c) Mike Brady 2014--2026
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libconfig.h>
#include <libgen.h>
#include <memory.h>
#include <net/if.h>
#include <popt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"

#include <libavutil/log.h>

#include "ptp-utilities.h"
#include "utilities/generate_device_uuid.h"
#include "utilities/generate_random_uuid.h"
#include <gcrypt.h>
#include <libavcodec/avcodec.h>
#include <sodium.h>
#include <uuid/uuid.h>




#include <openssl/evp.h>
#include <openssl/md5.h>


#include "activity_monitor.h"
#include "audio.h"
#include "common.h"
#include "rtp.h"
#include "rtsp.h"
#include "utilities/string_utilities.h"
#include "utilities/exit.h"







#include <syslog.h>



pid_t pid;

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 36
#endif

#define strnull(s) ((s) ? (s) : "(null)")

pthread_t rtsp_listener_thread;


int killOption = 0;
int daemonisewith = 0;
int daemonisewithout = 0;
int log_to_syslog_selected = 0;
int display_config_selected = 0;
int log_to_syslog_select_is_first_command_line_argument = 0;

char configuration_file_path[4096 + 1];
char *config_file_real_path = NULL;

char first_backend_name[256];

void print_version(void) {
  char *version_string = get_version_string();
  if (version_string) {
    printf("%s\n", version_string);
    free(version_string);
  } else {
    debug(1, "Can't print version string!");
  }
}

int has_fltp_capable_aac_decoder(void) {
  // return 1 if the AAC decoder advertises fltp decoding capability, which
  // is needed for decoding Buffered Audio streams
  debug(3, "checking availability of an  fltp-capable aac decoder");
  int has_capability = 0;
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
  if (codec != NULL) {
    const enum AVSampleFormat *formats = NULL;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    // New API (FFmpeg 7.1+) for getting formats
    debug(3, "getting sample formats the new way");
    int format_count = 0;
    if ((avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                      (const void **)&formats, &format_count) < 0) ||
        (format_count == 0))
      formats = NULL; // not clear if the returned pointer is nulled on error or on zero items
#else
    debug(3, "getting sample formats the old way");
    // older API
    formats = codec->sample_fmts;
#endif
    if (formats != NULL) {
      while ((has_capability == 0) && (*formats != AV_SAMPLE_FMT_NONE)) {
        if (*formats == AV_SAMPLE_FMT_FLTP) {
          has_capability = 1;
        }
        formats++;
      }
    }
  } else {
    debug(3, "no AAC codec found.");
  }
  return has_capability;
}


void usage(char *progname) {

  if (has_fltp_capable_aac_decoder() == 0) {
    printf("\nIMPORTANT NOTE: Shairport Sync can not run on this system.\n");
    printf("A Floating Planar (\"fltp\") AAC decoder is required, ");
    printf("but the system's ffmpeg library does not seem to include one.\n");
    printf("See: "
           "https://github.com/mikebrady/shairport-sync/blob/development/"
           "TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only\n\n");

  } else {
    // clang-format off
    printf("Please use the configuration file for settings where possible.\n");
    printf("Many more settings are available in the configuration file.\n");
    printf("\n");
    printf("Usage: %s [options...]\n", progname);
    printf("  or:  %s [options...] -- [audio output-specific options]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("    -h, --help              Show this help.\n");
    printf("    -V, --version           Show version information -- the version string.\n");
    printf("    --service-type          Select the type of AirPlay service: \"auto\", \"airplay2\" or \"classic\". (You can use \"airplay1\" in place of \"classic\".)\n");
    printf("    -X, --displayConfig     Output OS information, version string, command line, configuration file and active settings to the log.\n");
    printf("    --statistics            Print some interesting statistics. More will be printed if -v / -vv / -vvv are also chosen.\n");
    printf("    -v, --verbose           Print debug information; -v some; -vv more; -vvv lots -- generally too much.\n");
    printf("    -c, --configfile=FILE   Read configuration settings from FILE. Default is %s.\n", configuration_file_path);
    printf("    -a, --name=NAME         Set service name. Default is the hostname with first letter capitalised.\n");
    printf("    --password=PASSWORD     Require PASSWORD to connect. Default is no password.\n");
    printf("    -p, --port=PORT         Set RTSP listening port. Default 5000; 7000 for AirPlay 2.\n");
    printf("    -L, --latency=FRAMES    [Deprecated] Set the latency for audio sent from an unknown device.\n");
    printf("                            The default is to set it automatically.\n");
    printf("    -S, --stuffing=MODE     Set how to adjust current latency to match desired latency, where:\n");
    printf("                            \"vernier\" recodes a packet of frames to a new packet containing more or fewer frames. Recommended for low powered devices;\n");
    printf("                            \"basic\" inserts or deletes audio frames from packet frames with low processor overhead; and\n");
    printf("                            \"soxr\" uses libsoxr to minimally resample packet frames -- moderate floating point processor overhead.\n");
    printf("                            The default \"auto\" setting chooses vernier or soxr depending on processor capability.\n");
    printf("                            The \"soxr\" option is only available if built with soxr support.\n");
    printf("    -B, --on-start=PROGRAM  Run PROGRAM when playback is about to begin.\n");
    printf("    -E, --on-stop=PROGRAM   Run PROGRAM when playback has ended.\n");
    printf("                            For -B and -E options, specify the full path to the program and arguments, e.g. \"/usr/bin/logger\".\n");
    printf("                            Executable scripts work, but the file must be marked executable have the appropriate shebang (#!/bin/sh) on the first line.\n");
    printf("    -w, --wait-cmd          Wait until the -B or -E programs finish before continuing.\n");
    printf("    -o, --output=BACKEND    Select audio backend. They are listed at the end of this text. The first one is the default.\n");
    printf("    -m, --mdns=BACKEND      Use the mDNS backend named BACKEND to advertise the AirPlay service through Bonjour/ZeroConf.\n");
    printf("                            They are listed at the end of this text.\n");
    printf("                            If no mdns backend is specified, they are tried in order until one works.\n");
    printf("    -r, --resync=THRESHOLD  [Deprecated] resync if error exceeds this number of frames. Set to 0 to stop resyncing.\n");
    printf("    -t, --timeout=SECONDS   Go back to idle mode from play mode after a break in communications of this many seconds (default 60). Set to 0 never to exit play mode.\n");
    printf("    --tolerance=TOLERANCE   [Deprecated] Allow a synchronization error of TOLERANCE frames (default 88) before trying to correct it.\n");
    printf("    --logOutputLevel        Log the output level setting -- a debugging option, useful for determining the optimum maximum volume.\n");

    printf("    --log-to-syslog         Send debug and statistics information through syslog\n");
    printf("                            If used, this should be the first command line argument.\n");
    printf("    -u, --use-stderr        [Deprecated] This setting is not needed -- stderr is now used by default and syslog is selected using --log-to-syslog.\n");
    printf("\n");
    mdns_ls_backends();
    printf("\n");
    audio_ls_outputs();
    // clang-format on

  }
}

int parse_options(int argc, char **argv) {
  // there are potential memory leaks here -- it's called a second time, previously allocated
  // strings will dangle.
  char *cli_service_type_string = NULL;
  char *raw_service_name = NULL; /* Used to pick up the service name before possibly expanding it */
  char *stuffing = NULL;         /* used for picking up the stuffing option */
  signed char c; /* used for argument parsing */
  // int i = 0;                     /* used for tracking options */
  int resync_threshold_in_frames = 0;
  int tolerance_in_frames = 0;
  poptContext optCon; /* context for parsing command-line options */
  struct poptOption optionsTable[] = {
      {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', NULL, NULL},
      {"kill", 'k', POPT_ARG_NONE, &killOption, 0, NULL, NULL},
      {"daemon", 'd', POPT_ARG_NONE, &daemonisewith, 0, NULL, NULL},
      {"justDaemoniseNoPIDFile", 'j', POPT_ARG_NONE, &daemonisewithout, 0, NULL, NULL},
      {"configfile", 'c', POPT_ARG_STRING, &config.configfile, 0, NULL, NULL},
      {"statistics", 0, POPT_ARG_NONE, &config.statistics_requested, 0, NULL, NULL},
      {"logOutputLevel", 0, POPT_ARG_NONE, &config.logOutputLevel, 0, NULL, NULL},
      {"version", 'V', POPT_ARG_NONE, NULL, 0, NULL, NULL},
      {"displayConfig", 'X', POPT_ARG_NONE, &display_config_selected, 0, NULL, NULL},
      {"port", 'p', POPT_ARG_INT, &config.port, 0, NULL, NULL},
      {"name", 'a', POPT_ARG_STRING, &raw_service_name, 0, NULL, NULL},
      {"output", 'o', POPT_ARG_STRING, &config.output_name, 0, NULL, NULL},
      {"on-start", 'B', POPT_ARG_STRING, &config.cmd_start, 0, NULL, NULL},
      {"on-stop", 'E', POPT_ARG_STRING, &config.cmd_stop, 0, NULL, NULL},
      {"wait-cmd", 'w', POPT_ARG_NONE, &config.cmd_blocking, 0, NULL, NULL},
      {"mdns", 'm', POPT_ARG_STRING, &config.mdns_name, 0, NULL, NULL},
      {"latency", 'L', POPT_ARG_INT, &config.userSuppliedLatency, 0, NULL, NULL},
      {"stuffing", 'S', POPT_ARG_STRING, &stuffing, 'S', NULL, NULL},
      {"resync", 'r', POPT_ARG_INT, &resync_threshold_in_frames, 'r', NULL, NULL},
      {"timeout", 't', POPT_ARG_INT, &config.timeout, 't', NULL, NULL},
      {"password", 0, POPT_ARG_STRING, &config.password, 0, NULL, NULL},
      {"service-type", 0, POPT_ARG_STRING, &cli_service_type_string, 0, NULL, NULL},
      {"tolerance", 'z', POPT_ARG_INT, &tolerance_in_frames, 'z', NULL, NULL},
      {"use-stderr", 'u', POPT_ARG_NONE, NULL, 'u', NULL, NULL},
      {"log-to-syslog", 0, POPT_ARG_NONE, &log_to_syslog_selected, 0, NULL, NULL},
      POPT_AUTOHELP{NULL, 0, 0, NULL, 0, NULL, NULL}};

  // we have to parse the command line arguments to look for a config file
  int optind;
  optind = argc;
  int j;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  if (optCon == NULL)
    die("Can not get a secondary popt context.");
  poptSetOtherOptionHelp(optCon, "[OPTIONS]* ");

  /* Now do options processing just to get a debug log destination and level */
  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      increase_debug_level();
      break;
    case 'u':
      inform("Warning: the option -u is no longer needed and is deprecated. Debug and statistics "
             "output to STDERR is now the default.");
      break;
    case 'D':
      inform("Warning: the option -D or --disconnectFromOutput is deprecated.");
      break;
    case 'R':
      inform("Warning: the option -R or --reconnectToOutput is deprecated.");
      break;
    case 'A':
      inform("Warning: the option -A or --AirPlayLatency is deprecated and ignored. This setting "
             "is now "
             "automatically received from the AirPlay device.");
      break;
    case 'i':
      inform("Warning: the option -i or --iTunesLatency is deprecated and ignored. This setting is "
             "now "
             "automatically received from iTunes");
      break;
    case 'f':
      inform(
          "Warning: the option --forkedDaapdLatency is deprecated and ignored. This setting is now "
          "automatically received from forkedDaapd");
      break;
    case 'r':
      config.resync_threshold = (resync_threshold_in_frames * 1.0) / 44100;
      inform("Warning: the option -r or --resync is deprecated and ignored!\nPlease use the "
             "\"resync_threshold_in_seconds\" setting in the config file instead.");
      break;
    case 'z':
      config.tolerance = (tolerance_in_frames * 1.0) / 44100;
      inform("Warning: the option --tolerance is deprecated and ignored\nPlease use the "
             "\"drift_tolerance_in_seconds\" setting in the config file instead.");
      break;
    }
  }
  if (c < -1) {
    debug(1, "Oops");
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }

  poptFreeContext(optCon);

  if (config.timeout != 0) {
    if (config.timeout < 60) {
      inform("Note: the timeout value if invalid -- it must be 0 (i.e. no timeout) or at least 60. "
             "Set to the default value of 60 seconds instead.");
      config.timeout = 60;
    }
  }

  if (log_to_syslog_selected) {
    inform("the diagnostic \"log-to-syslog\" command_line_option is obsolete and is ignored. All logging is to STDERR, which is directed to the system log when Shairport Sync is running as a service.");
/*
#ifdef CONFIG_LIBDAEMON
    log_to_default = 0; // a specific log output modality has been selected.
#endif
    log_to_syslog();
*/
  }



  config.audio_backend_silent_lead_in_time_auto =
      1; // start outputting silence as soon as packets start arriving
  config.default_airplay_volume = -24.0;
  config.fixedLatencyOffset = 11025; // this sounds like it works properly.
  config.diagnostic_drop_packet_fraction = 0.0;
  config.active_state_timeout = 10.0;
  config.soxr_delay_threshold = 30 * 1000000; // the soxr measurement time (nanoseconds) of two
                                              // oneshots must not exceed this if soxr interpolation
                                              // is to be chosen automatically.
  config.volume_range_hw_priority =
      0; // if combining software and hardware volume control, give the software priority
         // i.e. when reducing volume, reduce the sw first before reducing the software.
         // this is because some hw mixers mute at the bottom of their range, and they don't always
  // advertise this fact
  config.resend_control_first_check_time =
      0.10; // wait this many seconds before requesting the resending of a missing packet
  config.resend_control_check_interval_time =
      0.25; // wait this many seconds before again requesting the resending of a missing packet
  config.resend_control_last_check_time =
      0.10; // give up if the packet is still missing this close to when it's needed
  config.missing_port_dacp_scan_interval_seconds =
      2.0; // check at this interval if no DACP port number is known

  config.minimum_free_buffer_headroom = 125; // leave approximately one second's worth of buffers
                                             // free after calculating the effective latency.
  // e.g. if we have 1024 buffers or 352 frames = 8.17 seconds and we have a nominal latency of 2.0
  // seconds then we can add an offset of 5.17 seconds and still leave a second's worth of buffers
  // for unexpected circumstances

  config.model = strdup("ShairportSync");
  // config.model = strdup("AirPort10,115");
  //  config.model = strdup("AudioAccessory5,1");

  // config.srcvers = strdup(PACKAGE_VERSION);
  // config.srcvers = strdup("760.13.1");

  config.srcvers = strdup("366.0");

  // config.osvers = strdup(VERSION);
  config.osvers = strdup("15.0");

  // make up a firmware version
#ifdef CONFIG_USE_GIT_VERSION_STRING
  if (git_version_string[0] != '\0')
    config.firmware_version = strdup(git_version_string);
  else
#endif
    config.firmware_version = strdup(PACKAGE_VERSION);


  config.loudness_reference_volume_db = -16;


  // config_setting_t *setting;
  const char *str = NULL;
  int value = 0;
  double dvalue = 0.0;

  // debug(1, "Looking for the configuration file \"%s\".", config.configfile);

  // use the MAC address placed in config.hw_addr to generate the default airplay_device_id
  uint64_t temporary_airplay_id = nctoh64(config.hw_addr);
  temporary_airplay_id =
      temporary_airplay_id >> 16; // we only use the first 6 bytes but have imported 8.

  config_init(&config_file_stuff);

  config_file_real_path = realpath(config.configfile, NULL);
  if (config_file_real_path == NULL) {
    debug(2, "can't resolve the configuration file \"%s\".", config.configfile);
  } else {
    debug(1, "looking for configuration file at full path \"%s\"", config_file_real_path);
    /* Read the file. If there is an error, report it and exit. */
    if (config_read_file(&config_file_stuff, config_file_real_path)) {
      config_set_auto_convert(&config_file_stuff,
                              1); // allow autoconversion from int/float to int/float
      // make config.cfg point to it
      config.cfg = &config_file_stuff;

      /* See if a specific service type has been requested */
      if (config_lookup_non_empty_string(config.cfg, "general.service_type", &str)) {
        config.service_type = string_to_service_type(str, "general service_type");
      }
      /* Get the Service Name. */
      if (config_lookup_non_empty_string(config.cfg, "general.name", &str)) {
        raw_service_name = (char *)str;
      }

      /* Get the mdns_backend setting. */
      if (config_lookup_non_empty_string(config.cfg, "general.mdns_backend", &str))
        config.mdns_name = (char *)str;

      /* Get the output_backend setting. */
      if (config_lookup_non_empty_string(config.cfg, "general.output_backend", &str))
        config.output_name = (char *)str;

      /* Get the port setting. */
      if (config_lookup_int(config.cfg, "general.port", &value)) {
        if ((value < 0) || (value > 65535))
          die("Invalid port number  \"%d\". It should be between 0 and 65535, default is 7000",
              value);
        else
          config.port = value;
      }

      /* Get the udp port base setting. */
      if (config_lookup_int(config.cfg, "general.udp_port_base", &value)) {
        if ((value < 0) || (value > 65535))
          die("Invalid port number  \"%d\". It should be between 0 and 65535, default is 6001",
              value);
        else
          config.udp_port_base = value;
      }

      /* Get the udp port range setting. This is number of ports that will be tried for free ports ,
       * starting at the port base. Only three ports are needed. */
      if (config_lookup_int(config.cfg, "general.udp_port_range", &value)) {
        if ((value < 3) || (value > 65535))
          die("Invalid port range  \"%d\". It should be between 3 and 65535, default is 10", value);
        else
          config.udp_port_range = value;
      }

      /* Get the password setting. */
      if (config_lookup_non_empty_string(config.cfg, "general.password", &str))
        config.password = (char *)str;

      if (config_lookup_string(config.cfg, "general.interpolation", &str)) {
        if (strcasecmp(str, "basic") == 0)
          config.packet_stuffing = ST_basic;
        else if (strcasecmp(str, "vernier") == 0)
          config.packet_stuffing = ST_vernier;
        else if (strcasecmp(str, "auto") == 0)
          config.packet_stuffing = ST_auto;
        else if (strcasecmp(str, "soxr") == 0)
          warn("The soxr option not available because this version of shairport-sync was built "
               "without libsoxr "
               "support. Change the \"general/interpolation\" setting in the configuration file.");
        else
          die("Invalid interpolation option choice \"%s\". It should be \"auto\", \"basic\", "
              "\"vernier\" or "
              "\"soxr\"",
              str);
      }


      /* Get the statistics setting. */
      if (config_set_lookup_bool(config.cfg, "general.statistics",
                                 &(config.statistics_requested))) {
        warn("The \"general\" \"statistics\" setting is deprecated. Please use the \"diagnostics\" "
             "\"statistics\" setting instead.");
      }

      /* The old drift tolerance setting. */
      if (config_lookup_int(config.cfg, "general.drift", &value)) {
        inform("The drift setting  is deprecated and ignored. Please use "
               "drift_tolerance_in_seconds instead");
      }

      /* The old resync setting. */
      if (config_lookup_int(config.cfg, "general.resync_threshold", &value)) {
        inform("The resync_threshold setting is deprecated and ignored. Please use "
               "resync_threshold_in_seconds instead");
      }

      /* Get the drift tolerance setting. */
      if (config_lookup_float(config.cfg, "general.drift_tolerance_in_seconds", &dvalue))
        config.tolerance = dvalue;

      /* Get the resync setting. */
      if (config_lookup_float(config.cfg, "general.resync_threshold_in_seconds", &dvalue))
        config.resync_threshold = dvalue;

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "general.log_verbosity", &value)) {
        warn("The \"general\" \"log_verbosity\" setting is deprecated. Please use the "
             "\"diagnostics\" \"log_verbosity\" setting instead.");
        if ((value >= 0) && (value <= 3))
          set_debug_level(value);
        else
          die("Invalid log verbosity setting option choice \"%d\". It should be between 0 and 3, "
              "inclusive.",
              value);
      }
      
      if (config_lookup_string(config.cfg, "diagnostics.get_plist_metadata", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.get_plist_metadata = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.get_plist_metadata = 1;
        else
          die("Invalid \"get_plist_metadata\" option choice \"%s\". It should be \"yes\" or "
              "\"no\"",
              str);
      }

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "diagnostics.log_verbosity", &value)) {
        if ((value >= 0) && (value <= 3))
          set_debug_level(value);
        else
          die("Invalid diagnostics log_verbosity setting option choice \"%d\". It should be "
              "between 0 and 3, "
              "inclusive.",
              value);
      }

      /* Get the config.debugger_show_file_and_line in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_file_and_line", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_file_and_line = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_file_and_line = 1;
        else
          die("Invalid diagnostics log_show_file_and_line option choice \"%s\". It should be "
              "\"yes\" or \"no\"",
              str);
      }

      /* Get the show elapsed time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_startup", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_elapsed_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_elapsed_time = 1;
        else
          die("Invalid diagnostics log_show_time_since_startup option choice \"%s\". It should be "
              "\"yes\" or \"no\"",
              str);
      }

      /* Get the show relative time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_last_message", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_relative_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_relative_time = 1;
        else
          die("Invalid diagnostics log_show_time_since_last_message option choice \"%s\". It "
              "should be \"yes\" or \"no\"",
              str);
      }

      /* Get the statistics setting. */
      if (config_lookup_string(config.cfg, "diagnostics.statistics", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.statistics_requested = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.statistics_requested = 1;
        else
          die("Invalid diagnostics statistics option choice \"%s\". It should be \"yes\" or "
              "\"no\"",
              str);
      }

      /* Get the disable_resend_requests setting. */
      if (config_lookup_string(config.cfg, "diagnostics.disable_resend_requests", &str)) {
        config.disable_resend_requests = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.disable_resend_requests = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.disable_resend_requests = 1;
        else
          die("Invalid diagnostic disable_resend_requests option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"",
              str);
      }

      /* Get the drop packets setting. */
      if (config_lookup_float(config.cfg, "diagnostics.drop_this_fraction_of_audio_packets",
                              &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.diagnostic_drop_packet_fraction = dvalue;
        else
          die("Invalid diagnostics drop_this_fraction_of_audio_packets setting \"%f\". It should "
              "be "
              "between 0.0 and 1.0, "
              "inclusive.",
              dvalue);
      }

      /* Get the diagnostics output default. */
      if (config_lookup_string(config.cfg, "diagnostics.log_output_to", &str)) {
      /*
#ifdef CONFIG_LIBDAEMON
        log_to_default = 0; // a specific log output modality has been selected.
#endif
        if (strcasecmp(str, "syslog") == 0)
          log_to_syslog();
        else if (strcasecmp(str, "stdout") == 0) {
          log_to_stdout();
        } else if (strcasecmp(str, "stderr") == 0) {
          log_to_stderr();
        } else {
          config.log_file_path = (char *)str;
          config.log_fd = -1;
          log_to_file();
        }
      */
        warn("the diagnostic \"log_output_to\" setting is obsolete and is ignored. All logging is to STDERR, which is directed to the system log when Shairport Sync is running as a service.");
      }
      
      
      /* Get the ignore_volume_control setting. */
      if (config_lookup_string(config.cfg, "general.ignore_volume_control", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.ignore_volume_control = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.ignore_volume_control = 1;
        else
          die("Invalid ignore_volume_control option choice \"%s\". It should be \"yes\" or \"no\"",
              str);
      }

      /* Get the optional volume_max_db setting. */
      if (config_lookup_float(config.cfg, "general.volume_max_db", &dvalue)) {
        // debug(1, "Max volume setting of %f dB", dvalue);
        config.volume_max_db = dvalue;
        config.volume_max_db_set = 1;
      }

      /* Get the optional default_volume setting. */
      if (config_lookup_float(config.cfg, "general.default_airplay_volume", &dvalue)) {
        // debug(1, "Default airplay volume setting of %f on the -30.0 to 0 scale", dvalue);
        if ((dvalue >= -30.0) && (dvalue <= 0.0)) {
          config.default_airplay_volume = dvalue;
        } else {
          warn("The default airplay volume setting must be between -30.0 and 0.0.");
        }
      }

      if (config_lookup_non_empty_string(config.cfg, "general.run_this_when_volume_is_set", &str)) {
        config.cmd_set_volume = (char *)str;
      }

      /* Get the playback_mode setting */
      if (config_lookup_string(config.cfg, "general.playback_mode", &str)) {
        if (strcasecmp(str, "stereo") == 0)
          config.playback_mode = ST_stereo;
        else if (strcasecmp(str, "mono") == 0)
          config.playback_mode = ST_mono;
        else if (strcasecmp(str, "reverse stereo") == 0)
          config.playback_mode = ST_reverse_stereo;
        else if (strcasecmp(str, "both left") == 0)
          config.playback_mode = ST_left_only;
        else if (strcasecmp(str, "both right") == 0)
          config.playback_mode = ST_right_only;
        else
          die("Invalid playback_mode choice \"%s\". It should be \"stereo\" (default), \"mono\", "
              "\"reverse stereo\", \"both left\", \"both right\"",
              str);
      }

      /* Get the volume control profile setting -- "standard" or "flat" */
      if (config_lookup_string(config.cfg, "general.volume_control_profile", &str)) {
        if (strcasecmp(str, "standard") == 0)
          config.volume_control_profile = VCP_standard;
        else if (strcasecmp(str, "flat") == 0)
          config.volume_control_profile = VCP_flat;
        else if (strcasecmp(str, "dasl_tapered") == 0)
          config.volume_control_profile = VCP_dasl_tapered;
        else
          die("Invalid volume_control_profile choice \"%s\". It should be \"standard\" (default), "
              "\"dasl_tapered\", or \"flat\"",
              str);
      }

      config_set_lookup_bool(config.cfg, "general.volume_control_combined_hardware_priority",
                             &config.volume_range_hw_priority);

      /* Get the interface to listen on, if specified Default is all interfaces */
      /* we keep the interface name and the index */

      if (config_lookup_string(config.cfg, "general.interface", &str)) {

        config.interface = strdup(str);
        config.interface_index = if_nametoindex(config.interface);

        if (config.interface_index == 0) {
          inform(
              "The mdns service interface \"%s\" was not found, so the setting has been ignored.",
              config.interface);
          free(config.interface);
          config.interface = NULL;
        }
      }

      /* Get the regtype -- the service type and protocol, separated by a dot. Default is
       * "_raop._tcp" */
      if (config_lookup_non_empty_string(config.cfg, "general.regtype", &str))
        config.regtype = strdup(str);

      /* Get the volume range, in dB, that should be used If not set, it means you just use the
       * range set by the mixer. */
      if (config_lookup_int(config.cfg, "general.volume_range_db", &value)) {
        if ((value < 30) || (value > 150))
          die("Invalid volume range  %d dB. It should be between 30 and 150 dB. Zero means use "
              "the mixer's native range. The setting reamins at %d.",
              value, config.volume_range_db);
        else
          config.volume_range_db = value;
      }

      /* Get the alac_decoder setting. */
      if (config_lookup_string(config.cfg, "general.alac_decoder", &str)) {
        if (strcasecmp(str, "hammerton") == 0) {
          if ((config.decoders_supported & 1 << decoder_hammerton) != 0)
            config.decoder_in_use = 1 << decoder_hammerton; // use David Hammerton's ALAC decoder
          else
            inform(
                "Support for the Hammerton ALAC decoder has not been compiled into this version of "
                "Shairport Sync. The default decoder will be used.");
        } else if (strcasecmp(str, "apple") == 0) {
          if ((config.decoders_supported & 1 << decoder_apple_alac) != 0)
            config.decoder_in_use = 1 << decoder_apple_alac; // use the Apple ALAC decoder
          else
            inform("Support for the Apple ALAC decoder has not been compiled into this version of "
                   "Shairport Sync. The default decoder will be used.");
        } else if (strcasecmp(str, "ffmpeg") == 0) {
          if ((config.decoders_supported & 1 << decoder_ffmpeg_alac) != 0)
            config.decoder_in_use = 1 << decoder_ffmpeg_alac; // use the FFMPEG ALAC decoder
          else
            inform("Support for the FFMPEG ALAC decoder has not been compiled into this version of "
                   "Shairport Sync. The default decoder will be used.");
        } else
          die("Invalid alac_decoder option choice \"%s\". It should be \"ffmpeg\", \"hammerton\" "
              "or \"apple\"",
              str);
      }

      /* Get the resend control settings. */
      if (config_lookup_float(config.cfg, "general.resend_control_first_check_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_first_check_time = dvalue;
        else
          warn("Invalid general resend_control_first_check_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_first_check_time);
      }

      if (config_lookup_float(config.cfg, "general.resend_control_check_interval_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_check_interval_time = dvalue;
        else
          warn("Invalid general resend_control_check_interval_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_check_interval_time);
      }

      if (config_lookup_float(config.cfg, "general.resend_control_last_check_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_last_check_time = dvalue;
        else
          warn("Invalid general resend_control_last_check_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_last_check_time);
      }

      if (config_lookup_float(config.cfg, "general.missing_port_dacp_scan_interval_seconds",
                              &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 300.0))
          config.missing_port_dacp_scan_interval_seconds = dvalue;
        else
          warn("Invalid general missing_port_dacp_scan_interval_seconds setting \"%f\". It should "
               "be "
               "between 0.0 and 300.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.missing_port_dacp_scan_interval_seconds);
      }

      /* Get the default latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.default", &value))
        config.userSuppliedLatency = value;



      if (config_lookup_non_empty_string(config.cfg, "sessioncontrol.run_this_before_play_begins",
                                         &str)) {
        config.cmd_start = (char *)str;
      }

      if (config_lookup_non_empty_string(config.cfg, "sessioncontrol.run_this_after_play_ends",
                                         &str)) {
        config.cmd_stop = (char *)str;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_before_entering_active_state", &str)) {
        config.cmd_active_start = (char *)str;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_after_exiting_active_state", &str)) {
        config.cmd_active_stop = (char *)str;
      }

      if (config_lookup_float(config.cfg, "sessioncontrol.active_state_timeout", &dvalue)) {
        if (dvalue < 0.0)
          warn("Invalid value \"%f\" for \"active_state_timeout\". It must be positive. "
               "The default of %f will be used instead.",
               dvalue, config.active_state_timeout);
        else
          config.active_state_timeout = dvalue;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_if_an_unfixable_error_is_detected", &str)) {
        config.cmd_unfixable = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.wait_for_completion", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_blocking = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_blocking = 1;
        else
          warn("Invalid \"wait_for_completion\" option choice \"%s\". It should be "
               "\"yes\" or \"no\". It is set to \"no\".",
               str);
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.before_play_begins_returns_output",
                               &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_start_returns_output = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_start_returns_output = 1;
        else
          die("Invalid \"before_play_begins_returns_output\" option choice \"%s\". It "
              "should be "
              "\"yes\" or \"no\"",
              str);
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.allow_session_interruption", &str)) {
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.allow_session_interruption = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.allow_session_interruption = 1;
        else
          die("Invalid \"allow_interruption\" option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"",
              str);
      }

      if (config_lookup_int(config.cfg, "sessioncontrol.session_timeout", &value)) {
        if (value == 0) {
          config.dont_check_timeout = 1;
        } else if (value < 60) {
          warn("Invalid value \"%d\" for \"session_timeout\". It must be 0 (i.e. no timeout) or at "
               "least 60. "
               "The default of %d will be used instead.",
               value, config.timeout);
          config.dont_check_timeout = 0;
        } else {
          config.timeout = value;
          config.dont_check_timeout = 0;
        }
      }


      if (config_lookup_string(config.cfg, "dsp.loudness", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.loudness_enabled = 0;
        else if (strcasecmp(str, "yes") == 0) {
          config.loudness_enabled = 1;
        }
        warn("the \"dsp\" \"loudness\" setting is deprecated and will be removed due to its "
             "potential ambiguity. Please use \"loudness_enabled\" instead.");
      }

      if (config_lookup_string(config.cfg, "dsp.loudness_enabled", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.loudness_enabled = 0;
        else if (strcasecmp(str, "yes") == 0) {
          config.loudness_enabled = 1;
        } else
          die("Invalid dsp.loudness_enabled \"%s\". It should be \"yes\" or \"no\"", str);
      }

      if (config_lookup_float(config.cfg, "dsp.loudness_reference_volume_db", &dvalue)) {
        config.loudness_reference_volume_db = dvalue;
        if (dvalue > 0 || dvalue < -100)
          die("Invalid value \"%f\" for dsp.loudness_reference_volume_db. It should be between "
              "-100 and 0",
              dvalue);
      }

      if (config.loudness_enabled == 1 &&
          config_lookup_non_empty_string(config.cfg, "alsa.mixer_control_name", &str))
        die("The loudness filter is activated but cannot be used because the volume is being "
            "controlled by a hardware mixer. "
            "You must not use a hardware mixer when using the loudness filter.");



      long long aid;

      // replace the airplay_device_id with this, if provided
      if (config_lookup_int64(config.cfg, "general.airplay_device_id", &aid)) {
        temporary_airplay_id = aid;
      }

      // add the airplay_device_id_offset if provided
      if (config_lookup_int64(config.cfg, "general.airplay_device_id_offset", &aid)) {
        temporary_airplay_id += aid;
      }


    } else {
      if (config_error_type(&config_file_stuff) == CONFIG_ERR_FILE_IO)
        die("Error reading configuration file \"%s\": \"%s\".", config_file_real_path,
            config_error_text(&config_file_stuff));
      else {
        die("Line %d of the configuration file \"%s\":\n%s", config_error_line(&config_file_stuff),
            config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      }
    }
  }

  // now, do the command line options again, but this time do them fully -- it's a unix convention
  // that command line
  // arguments have precedence over configuration file settings.
  optind = argc;
  for (j = 0; j < argc; j++)
    if (strcmp(argv[j], "--") == 0)
      optind = j;

  optCon = poptGetContext(NULL, optind, (const char **)argv, optionsTable, 0);
  if (optCon == NULL)
    die("Can not get a popt context.");
  poptSetOtherOptionHelp(optCon, "[OPTIONS]* ");

  /* Now do options processing, get portname */
  int tdebuglev = 0;
  while ((c = poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'v':
      tdebuglev++;
      break;
    case 't':
      if (config.timeout == 0) {
        config.dont_check_timeout = 1;
        config.allow_session_interruption = 1;
      } else {
        config.dont_check_timeout = 0;
        config.allow_session_interruption = 0;
      }
      break;
    case 'S':
      if (strcmp(stuffing, "basic") == 0)
        config.packet_stuffing = ST_basic;
      else if (strcmp(stuffing, "vernier") == 0)
        config.packet_stuffing = ST_vernier;
      else if (strcmp(stuffing, "auto") == 0)
        config.packet_stuffing = ST_auto;
      else if (strcmp(stuffing, "soxr") == 0)
        die("The soxr option not available because this version of shairport-sync was built "
            "without libsoxr "
            "support. Change the -S option setting.");
      else
        die("Illegal stuffing option \"%s\" -- must be \"auto\", \"vernier\", \"basic\" or "
            "\"soxr\"",
            stuffing);
      break;
    }
  }
  if (c < -1) {
    die("%s: %s", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
  }

  if (cli_service_type_string != NULL)
    config.service_type = string_to_service_type(cli_service_type_string,
                                                 "command line option \"--service-type\" argument");

  poptFreeContext(optCon);


  // here, we are finally finished reading the options

  // finish the Airplay 2 options


  char shared_memory_interface_name[256] = "";
  snprintf(shared_memory_interface_name, sizeof(shared_memory_interface_name), "/%s-%" PRIx64 "",
           config.appName, temporary_airplay_id);
  // debug(1, "smi name: \"%s\"", shared_memory_interface_name);

  config.nqptp_shared_memory_interface_name = strdup(NQPTP_INTERFACE_NAME);

  // create the config.ap1_prefix[i]
  char apids[6 * 2 + 5 + 1]; // six pairs of digits, 5 colons and a NUL
  apids[6 * 2 + 5] = 0;      // NUL termination
  int i;
  char hexchar[] = "0123456789abcdef";
  for (i = 5; i >= 0; i--) {
    // In AirPlay 2 mode, the AP1 name prefix must be
    // the same as the AirPlay 2 device id less the colons.
    config.ap1_prefix[i] = temporary_airplay_id & 0xFF;
    apids[i * 3 + 1] = hexchar[temporary_airplay_id & 0xF];
    temporary_airplay_id = temporary_airplay_id >> 4;
    apids[i * 3] = hexchar[temporary_airplay_id & 0xF];
    temporary_airplay_id = temporary_airplay_id >> 4;
    if (i != 0)
      apids[i * 3 - 1] = ':';
  }

  config.airplay_device_id = strdup(apids);

  // Create an airplay psi UUID based on the ap1_prefix.

  // a uuid_t and an md5 hash are both 128 bits, 16 bytes
  uuid_t result;
  memset(result, 0, sizeof(result));
  if (sizeof(config.ap1_prefix) < sizeof(result))
    memcpy(result, config.ap1_prefix, sizeof(config.ap1_prefix));
  else
    memcpy(result, config.ap1_prefix, sizeof(result));

  // OpenSSL is mandatory for AirPlay 2
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
  EVP_DigestUpdate(mdctx, config.ap1_prefix, sizeof(config.ap1_prefix));
  unsigned int md5_digest_len = EVP_MD_size(EVP_md5());
  EVP_DigestFinal_ex(mdctx, result, &md5_digest_len);
  EVP_MD_CTX_free(mdctx);

  // now, convert it into a type 4 UUID
  // see https://stackoverflow.com/questions/10867405/generating-v5-uuid-what-is-name-and-namespace
  // //set high-nibble to 5 to indicate type 5

  result[6] &= 0x0F;
  result[6] |= 0x40;

  // set upper two bits to "10"
  result[8] &= 0x3F;
  result[8] |= 0x80;

  char *psi_uuid = malloc(UUID_STR_LEN + 1); // leave space for the NUL at the end
  // Produces a UUID string at uuid consisting of lower-case letters
  uuid_unparse_lower(result, psi_uuid);
  config.airplay_psi = psi_uuid;

  debug(3, "size of pk is %zu.", sizeof(config.airplay_pk));
  pair_public_key_get(PAIR_SERVER_HOMEKIT, config.airplay_pk, config.airplay_device_id);
  char buf[128];
  char *ptr = buf;
  size_t pk_index;
  for (pk_index = 0; pk_index < sizeof(config.airplay_pk); pk_index++)
    ptr += sprintf(ptr, "%02x", config.airplay_pk[pk_index]);
  *ptr = '\0';
  config.pk_string = strdup(buf);

  // the features code is a 64-bit number, but in the mDNS advertisement, the least significant 32
  // bit are given first for example, if the features number is 0x1C340405F4A00, it will be given as
  // features=0x405F4A00,0x1C340 in the mDNS string, and in a signed decimal number in the plist:
  // 496155702020608 this setting here is the source of both the plist features response and the
  // mDNS string.

  config.airplay_features = 0x00018340405C4A00; // no AP2 metadata (b50), no AP1 text (b17), no AP1
                                                // progress (b16), no AP1 artwork (b15)
  //     0x0001C340405C4A00; // no AP2 metadata (b50), no AP1 text (b17), no AP1 progress (b16), no
  //     AP1 artwork (b15) 0x0001C340445D0A00;
  // config.airplay_features |= (1 << 26); // 0x0x4000000

  // features=0x0001C340445D0A00 -- AirPort Express


  // now generate the fex field
  uint8_t fexbytes[8];
  uint64_t temp = config.airplay_features;
  debug(4, "airplay_features are %" PRIx64 ".", temp);
  for (i = 0; i < 8; i++) {
    fexbytes[i] = temp & 0xff;
    temp = temp >> 8;
  }

  config.airplay_fex = base64_enc(fexbytes, 8);
  if (config.airplay_fex == NULL)
    die("could not allocate memory for \"airplay_fex\"");
  // strip the padding.
  char *padding = strchr(config.airplay_fex, '=');
  if (padding)
    *padding = 0;
  debug(2, "airplay_fex is \"%s\"", config.airplay_fex);

  // now the status flags
  // Advertised with mDNS and returned with GET /info, see
  // https://openairplay.github.io/airplay-spec/status_flags.html

  config.airplay_statusflags = 0;
  config.airplay_statusflags |= 1 << 2; // Audio cable is attached
  if (config.password != NULL) {
    config.airplay_statusflags |= 1 << 7; // Password required
  }
  // config.airplay_statusflags |= 1 << 10; // DeviceWasSetupForHKAccessControl
  // config.airplay_statusflags |= 1 << 11; // DeviceSupportsRelay
  // config.airplay_statusflags |= 1 << 19; // Unknown. Seems to control whether individual volume
  // controls are shown and whether the SPS devices shows when its active.

  config.airplay_pi = generate_device_uuid(config.airplay_device_id);
  config.airplay_pgid = generate_random_uuid();

  /* Check if we are called with -d or --daemon or -j or justDaemoniseNoPIDFile options*/
  if ((daemonisewith != 0) || (daemonisewithout != 0)) {
    fprintf(stderr,
            "%s was built without libdaemon, so does not support daemonisation using the "
            "-d, --daemon, -j or --justDaemoniseNoPIDFile options\n",
            config.appName);
    exit(EXIT_FAILURE);
  }



  /* if the regtype hasn't been set, do it now */
  if (config.regtype == NULL)
    config.regtype = strdup("_raop._tcp");
  if (config.regtype2 == NULL)
    config.regtype2 = strdup("_airplay._tcp");

  if (tdebuglev != 0)
    set_debug_level(tdebuglev);

  // now set the initial volume to the default volume
  config.airplay_volume =
      config.default_airplay_volume; // if no volume is ever set or requested, default to initial
                                     // default value if nothing else comes in first.

ptp_send_control_message_string("T");
if (ptp_shm_interface_open() != 0) {
  die("NQPTP is required for AirPlay 2 and must be running with readable, complete shared memory: %s.", strerror(errno));
}
int ptp_clock_version = ptp_get_clock_version();
if (ptp_clock_version == 0)
  die("NQPTP shared memory is not initialised or its clock data is inconsistent.");
if (ptp_clock_version != NQPTP_SHM_STRUCTURES_VERSION)
  die("NQPTP shared memory version %d is incompatible; version %d is required.",
      ptp_clock_version, NQPTP_SHM_STRUCTURES_VERSION);

  config.service_name = service_name(raw_service_name);


  return optind + 1;
}




void exit_rtsp_listener() {
  debug(3, "exit_rtsp_listener begins");
  pthread_cancel(rtsp_listener_thread);
  pthread_join(rtsp_listener_thread, NULL); // not sure you need this
  debug(2, "exit_rtsp_listener ends");
}

void exit_function() {
    // the following is to ensure that if libdaemon has been included
    // that most of this code will be skipped when the parent process is exiting
    // exec
      /*
      Actually, there is no terminate_mqtt() function.
      #ifdef CONFIG_MQTT
              if (config.mqtt_enabled) {
                      terminate_mqtt();
              }
      #endif
      */

      debug(2, "Stopping the activity monitor.");
      activity_monitor_stop();
      debug(2, "Stopping the activity monitor done.");





      if ((config.output) && (config.output->deinit)) {
        debug(2, "Deinitialise the audio backend.");
        config.output->deinit();
        debug(2, "Deinitialise the audio backend done.");
      }


      if (config.service_name)
        free(config.service_name);



      if (config.regtype)
        free(config.regtype);
      if (config.model)
        free(config.model);
      if (config.srcvers)
        free(config.srcvers);
      if (config.osvers)
        free(config.osvers);

      if (config.regtype2)
        free(config.regtype2);
      if (config.nqptp_shared_memory_interface_name)
        free(config.nqptp_shared_memory_interface_name);
      if (config.airplay_device_id)
        free(config.airplay_device_id);
      if (config.airplay_pi)
        free(config.airplay_pi);
      if (config.airplay_pgid)
        free(config.airplay_pgid);
      if (config.airplay_psi)
        free(config.airplay_psi);
      if (config.pk_string)
        free(config.pk_string);
      if (config.firmware_version)
        free(config.firmware_version);
      ptp_shm_interface_close(); // close it if it's open

    if (config.cfg)
      config_destroy(config.cfg);
    if (config_file_real_path)
      free(config_file_real_path);
    if (config.appName)
      free(config.appName);

    // probably should be freeing malloc'ed memory here, including strdup-created strings...

    mdns_unregister(); // once the dacp handler is done and all player threads are done it should
                       // be safe
    debug(2, "normal exit");
}

// for removing zombie script processes
// see: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
// used with thanks.

void handle_sigchld(__attribute__((unused)) int sig) {
  int saved_errno = errno;
  while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {
  }
  errno = saved_errno;
}

// for clean exits
void intHandler(__attribute__((unused)) int k) {
  debug(2, "exit on SIGINT");
  exit_request(EXIT_SUCCESS);
}

void termHandler(__attribute__((unused)) int k) {
  debug(2, "exit on SIGTERM");
  exit_request(EXIT_SUCCESS);
}

void _display_config(const char *filename, const int linenumber, __attribute__((unused)) int argc,
                     __attribute__((unused)) char **argv) {

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif

  _inform(filename, linenumber, ">> Display Config Start.");

  // see the man entry on popen
  FILE *fp;
  int status;
  char result[1024];

  fp = popen("uname -a 2>/dev/null", "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From \"uname -a\":");
      if (result[strlen(result) - 1] <= ' ')
        result[strlen(result) - 1] = '\0'; // remove the last character if it's not printable
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  fp = popen("(cat /etc/os-release | grep PRETTY_NAME | sed 's/PRETTY_NAME=//' | sed 's/\"//g') "
             "2>/dev/null",
             "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From /etc/os-release:");
      if (result[strlen(result) - 1] <= ' ')
        result[strlen(result) - 1] = '\0'; // remove the last character if it's not printable
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  fp = popen("cat /sys/firmware/devicetree/base/model 2>/dev/null", "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From /sys/firmware/devicetree/base/model:");
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  char *version_string = get_version_string();
  if (version_string) {
    _inform(filename, linenumber, "");
    _inform(filename, linenumber, "Shairport Sync Version String:");
    _inform(filename, linenumber, " %s", version_string);
    free(version_string);
  } else {
    debug(1, "Can't print version string!\n");
  }

  if (argc != 0) {
    char *obfp = result;
    int i;
    for (i = 0; i < argc - 1; i++) {
      snprintf(obfp, strlen(argv[i]) + 2, "%s ", argv[i]);
      obfp += strlen(argv[i]) + 1;
    }
    snprintf(obfp, strlen(argv[i]) + 1, "%s", argv[i]);
    obfp += strlen(argv[i]);
    *obfp = 0;

    _inform(filename, linenumber, "");
    _inform(filename, linenumber, "Command Line:");
    _inform(filename, linenumber, " %s", result);
  }

  if (config.cfg == NULL)
    _inform(filename, linenumber, "No configuration file.");
  else {
    int configpipe[2];
    if (pipe(configpipe) == 0) {
      FILE *cw;
      cw = fdopen(configpipe[1], "w");
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "Configuration File:");
      _inform(filename, linenumber, " %s", config_file_real_path);
      _inform(filename, linenumber, "");
      config_write(config.cfg, cw);
      fclose(cw);
      // get back the raw configuration file settings text
      FILE *cr;
      cr = fdopen(configpipe[0], "r");
      int i = 0;
      int ch = 0;
      do {
        ch = fgetc(cr);
        if (ch == EOF) {
          result[i] = '\0';
        } else {
          result[i] = (char)ch;
          i++;
        }
      } while (ch != EOF);
      fclose(cr);
      // debug(1,"result is \"%s\".",result);
      // remove empty stanzas
      char *i0 = str_replace(result, "general : \n{\n};\n", "");
      char *i1 = str_replace(i0, "sessioncontrol : \n{\n};\n", "");
      char *i2 = str_replace(i1, "alsa : \n{\n};\n", "");
      char *i3 = str_replace(i2, "sndio : \n{\n};\n", "");
      char *i4 = str_replace(i3, "pulseaudio : \n{\n};\n", "");
      char *i5 = str_replace(i4, "jack : \n{\n};\n", "");
      char *i6 = str_replace(i5, "pipe : \n{\n};\n", "");
      char *i7 = str_replace(i6, "dsp : \n{\n};\n", "");
      char *i8 = str_replace(i7, "metadata : \n{\n};\n", "");
      char *i9 = str_replace(i8, "mqtt : \n{\n};\n", "");
      char *i10 = str_replace(i9, "diagnostics : \n{\n};\n", "");
      char *i11 = str_replace(i10, "pipewire : \n{\n};\n", "");
      char *i12 = str_replace(i11, "stdout : \n{\n};\n", "");
      char *i13 = str_replace(i12, "pipe : \n{\n};\n", "");
      char *i14 = str_replace(i13, "ao : \n{\n};\n", "");
      // debug(1,"i10 is \"%s\".",i10);

      // free intermediate strings
      free(i13);
      free(i12);
      free(i11);
      free(i10);
      free(i9);
      free(i8);
      free(i7);
      free(i6);
      free(i5);
      free(i4);
      free(i3);
      free(i2);
      free(i1);
      free(i0);

      // print it out
      if (strlen(i14) == 0)
        _inform(filename, linenumber, "The Configuration file contains no active settings.");
      else {
        _inform(filename, linenumber, "Configuration File Settings:");
        char *p = i14;
        while (*p != '\0') {
          i = 0;
          while ((*p != '\0') && (*p != '\n')) {
            result[i] = *p;
            p++;
            i++;
          }
          if (i != 0) {
            result[i] = '\0';
            _inform(filename, linenumber, " %s", result);
          }
          if (*p == '\n')
            p++;
        }
      }

      free(i14); // free the cleaned-up configuration string

      /*
            while (fgets(result, 1024, cr) != NULL) {
              // replace funny character at the end, if it's there
              if (result[strlen(result) - 1] <= ' ')
                result[strlen(result) - 1] = '\0'; // remove the last character if it's not
         printable _inform(filename, linenumber, " %s", result);
            }
      */
    } else {
      debug(1, "Error making pipe.\n");
    }
  }
  _inform(filename, linenumber, "");
  _inform(filename, linenumber, ">> Display Config End.");

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

#define display_config(argc, argv) _display_config(__FILE__, __LINE__, argc, argv)


/*
typedef struct {           // channel layout names and equates -- see
                           // https://www.ffmpeg.org/doxygen/2.4/group__channel__mask__c.html
  const char *name;        // e.g. "AV_CH_LAYOUT_5POINT1"
  uint64_t channel_layout; // e.g. AV_CH_LAYOUT_5POINT1
} channel_layout_t;
channel_layout_t channel_layouts[] = {
    {"AV_CH_LAYOUT_MONO", AV_CH_LAYOUT_MONO},
    {"AV_CH_LAYOUT_STEREO", AV_CH_LAYOUT_STEREO},
    {"AV_CH_LAYOUT_2POINT1", AV_CH_LAYOUT_2POINT1},
    {"AV_CH_LAYOUT_2_1", AV_CH_LAYOUT_2_1},
    {"AV_CH_LAYOUT_SURROUND", AV_CH_LAYOUT_SURROUND},
    {"AV_CH_LAYOUT_3POINT1", AV_CH_LAYOUT_3POINT1},
    {"AV_CH_LAYOUT_4POINT0", AV_CH_LAYOUT_4POINT0},
    {"AV_CH_LAYOUT_4POINT1", AV_CH_LAYOUT_4POINT1},
    {"AV_CH_LAYOUT_2_2", AV_CH_LAYOUT_2_2},
    {"AV_CH_LAYOUT_QUAD", AV_CH_LAYOUT_QUAD},
    {"AV_CH_LAYOUT_5POINT0", AV_CH_LAYOUT_5POINT0},
    {"AV_CH_LAYOUT_5POINT1", AV_CH_LAYOUT_5POINT1},
    {"AV_CH_LAYOUT_5POINT0_BACK", AV_CH_LAYOUT_5POINT0_BACK},
    {"AV_CH_LAYOUT_5POINT1_BACK", AV_CH_LAYOUT_5POINT1_BACK},
    {"AV_CH_LAYOUT_6POINT0", AV_CH_LAYOUT_6POINT0},
    {"AV_CH_LAYOUT_6POINT0_FRONT", AV_CH_LAYOUT_6POINT0_FRONT},
    {"AV_CH_LAYOUT_HEXAGONAL", AV_CH_LAYOUT_HEXAGONAL},
    {"AV_CH_LAYOUT_6POINT1", AV_CH_LAYOUT_6POINT1},
    {"AV_CH_LAYOUT_6POINT1_BACK", AV_CH_LAYOUT_6POINT1_BACK},
    {"AV_CH_LAYOUT_6POINT1_FRONT", AV_CH_LAYOUT_6POINT1_FRONT},
    {"AV_CH_LAYOUT_7POINT0", AV_CH_LAYOUT_7POINT0},
    {"AV_CH_LAYOUT_7POINT0_FRONT", AV_CH_LAYOUT_7POINT0_FRONT},
    {"AV_CH_LAYOUT_7POINT1", AV_CH_LAYOUT_7POINT1},
    {"AV_CH_LAYOUT_7POINT1_WIDE", AV_CH_LAYOUT_7POINT1_WIDE},
    {"AV_CH_LAYOUT_7POINT1_WIDE_BACK", AV_CH_LAYOUT_7POINT1_WIDE_BACK},
    {"AV_CH_LAYOUT_OCTAGONAL", AV_CH_LAYOUT_OCTAGONAL},
    //        {"AV_CH_LAYOUT_STEREO_DOWNMIX",	AV_CH_LAYOUT_STEREO_DOWNMIX},
};

uint64_t av_channel_layout_lookup(const char *str) {
  unsigned int m;
  uint64_t response = 0;
  for (m = 0; (m < sizeof(channel_layouts) / sizeof(channel_layout_t)) && (response == 0); m++) {
    if (strcasecmp(str, channel_layouts[m].name) == 0) {
      response = channel_layouts[m].channel_layout;
    }
  }
  return response;
}

const char *av_channel_layout_name(uint64_t channel_layout) {
  const char *response = NULL;
  unsigned int m;
  for (m = 0; (m < sizeof(channel_layouts) / sizeof(channel_layout_t)) && (response == NULL); m++) {
    if (channel_layouts[m].channel_layout == channel_layout) {
      response = channel_layouts[m].name;
    }
  }
  return response;
}
*/


int main(int argc, char **argv) {
  exit_init(); // initialise the exit handler to give us a clean safe exit on request
  // initialise debug messages stuff -- level 0, no elapsed time, relative time, file and line
  // debug_init(int level, int show_elapsed_time, int show_relative_time, int show_file_and_line)
  debug_init(0, 0, 1, 1, exit_request);
  memset(&config, 0, sizeof(config)); // also clears all strings, BTW
  /* Check if we are called with -V or --version parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-V") == 0) || (strcmp(argv[1], "--version") == 0))) {
    print_version();
    exit(EXIT_SUCCESS);
  }

  // this is a bit weird, but necessary -- basename() may modify the argument passed in
  char *basec = strdup(argv[0]);
  char *bname = basename(basec);
  config.appName = strdup(bname);
  if (config.appName == NULL)
    die("can not allocate memory for the app name!");
  free(basec);

  strcpy(configuration_file_path, SYSCONFDIR);
  // strcat(configuration_file_path, "/shairport-sync"); // thinking about adding a special
  // shairport-sync directory
  strcat(configuration_file_path, "/");
  strcat(configuration_file_path, config.appName);
  strcat(configuration_file_path, ".conf");
  config.configfile = configuration_file_path;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 10, 0)
  avcodec_init();
#endif
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
  avcodec_register_all();
#endif
  if (debug_level() == 0)
    av_log_set_level(AV_LOG_ERROR);
  else
    av_log_set_level(AV_LOG_VERBOSE);

  /* Check if we are called with -h or --help parameter */
  if (argc >= 2 && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
    usage(argv[0]);
    exit(EXIT_SUCCESS);
  }

/*
  // Check if we are called with -log-to-syslog
  if (argc >= 2 && (strcmp(argv[1], "--log-to-syslog") == 0)) {
    log_to_syslog_select_is_first_command_line_argument = 1;
    log_to_syslog();
  } else {
    log_to_stderr();
  }
*/

  pid = getpid();
  config.log_fd = -1;

  setlogmask(LOG_UPTO(LOG_DEBUG));
  openlog(NULL, 0, LOG_DAEMON);
  debug(1, "adding the exit function");
  atexit(exit_function);

  config.service_type = APST_airplay2; // this may be changed by the settings...

  // get a device id -- the first non-local MAC address
  get_device_id((uint8_t *)&config.hw_addr, 6);

  // get the endianness
  union {
    uint32_t u32;
    uint8_t arr[4];
  } xn;

  xn.arr[0] = 0x44; /* Lowest-address byte */
  xn.arr[1] = 0x33;
  xn.arr[2] = 0x22;
  xn.arr[3] = 0x11; /* Highest-address byte */

  if (xn.u32 == 0x11223344)
    config.endianness = SS_LITTLE_ENDIAN;
  else if (xn.u32 == 0x33441122)
    config.endianness = SS_PDP_ENDIAN;
  else if (xn.u32 == 0x44332211)
    config.endianness = SS_BIG_ENDIAN;
  else
    die("Can not recognise the endianness of the processor.");

  // set non-zero / non-NULL default values here
  // but note that audio back ends also have a chance to set defaults

  // get the first output backend in the list and make it the default
  audio_output *first_backend = audio_get_output(NULL);
  if (first_backend == NULL) {
    die("No audio backend found! Check your build of Shairport Sync.");
  } else {
    strncpy(first_backend_name, first_backend->name, sizeof(first_backend_name) - 1);
    config.output_name = first_backend_name;
  }

  // config.statistics_requested = 0; // don't print stats in the log
  // config.userSuppliedLatency = 0; // zero means none supplied

  config.debugger_show_file_and_line =
      1; // by default, log the file and line of the originating message
  config.debugger_show_relative_time =
      1;               // by default, log the  time back to the previous debug message
  config.timeout = 60; // wait this number of seconds to wait for a dropped RTSP connection to come
                       // back before declaring it lost.
  config.buffer_start_fill = 220;

  config.resync_threshold = 0.050; // default
  config.tolerance = 0.002;

  config.packet_stuffing = ST_vernier; // you need to explicitly ask for "basic" (ST_basic)

  // set_requested_connection_state_to_output(
  //     1); // we expect to be able to connect to the output device
  config.audio_backend_buffer_desired_length = 0.15; // seconds
  config.audio_decoded_buffer_desired_length = 0.75; // seconds
  config.udp_port_base = 6001;
  config.udp_port_range = 10;

  config.current_output_configuration = 0; // no output configuration selected...

  // config.output_format = SPS_FORMAT_S16_LE; // default
  config.output_rate_auto_requested = 1;   // default auto select format
  config.output_format_auto_requested = 1; // default auto select format

  config.decoders_supported |= 1 << decoder_ffmpeg_alac;
  config.decoder_in_use = 1 << decoder_ffmpeg_alac; // If present, use this in preference

  config.output_channel_mapping_enable = 1; // enabled by default
  config.output_channel_map_size = 0;       // use the device's channel map if it has one
  config.mixdown_enable = 1;                // enabled by default
  config.mixdown_channel_layout =
      0; // 0 means pick a mixdown based on the number of output channels

  // initialise random number generator

  r64init(0);

  // parse arguments into config -- needed to locate pid_dir
  int audio_arg = parse_options(argc, argv);

  if (display_config_selected != 0) {
    display_config(argc, argv);
    if (argc == 2) {
      inform(">> Goodbye!");
      exit(EXIT_SUCCESS);
    }
  }

  /* Check if we are called with -k or --kill option */
  if (killOption != 0) {
    warn("%s was built without libdaemon, so it does not support the -k or --kill option.",
         config.appName);
    return 1;
  }


  if (config.service_type == APST_airplay2) {
    config.port = 7000;
  } else {
    config.port = 5000;
  }

  if (config.service_type == APST_airplay2) {
    if (has_fltp_capable_aac_decoder() == 0) {
      die("Shairport Sync can not run on this system. Run \"shairport-sync -h\" for more "
          "information.");
    }
  }
  uint64_t apf = config.airplay_features;
  uint64_t apfh = config.airplay_features;
  apfh = apfh >> 32;
  uint32_t apf32 = apf;
  uint32_t apfh32 = apfh;
  if (config.service_type == APST_airplay2) {
    debug(1,
          "Startup in AirPlay 2 mode, with features 0x%" PRIx32 ",0x%" PRIx32 " on device \"%s\".",
          apf32, apfh32, config.airplay_device_id);
  } else {
    debug(1, "Startup in Classic AirPlay (aka \"AirPlay 1\") mode. (AirPlay2 build.)");
  }

  // control-c (SIGINT) cleanly
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = intHandler;
  sigaction(SIGINT, &act, NULL);

  // terminate (SIGTERM)
  struct sigaction act2;
  memset(&act2, 0, sizeof(struct sigaction));
  act2.sa_handler = termHandler;
  sigaction(SIGTERM, &act2, NULL);

  // stop a pipe signal from killing the program
  signal(SIGPIPE, SIG_IGN);

  // install a zombie process reaper
  // see: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
  struct sigaction sa;
  sa.sa_handler = &handle_sigchld;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, 0) == -1) {
    perror(0);
    exit(1);
  }

  // make sure the program can create files that group and world can read
  umask(S_IWGRP | S_IWOTH);

  /* print out version */

  char *version_dbs = get_version_string();
  if (version_dbs) {
    debug(1, "Version String: \"%s\"", version_dbs);
    free(version_dbs);
  } else {
    debug(1, "Can't print the version information!");
  }

  // print command line

  if (argc != 0) {
    char result[1024];
    char *obfp = result;
    int i;
    for (i = 0; i < argc - 1; i++) {
      snprintf(obfp, strlen(argv[i]) + 2, "%s ", argv[i]);
      obfp += strlen(argv[i]) + 1;
    }
    snprintf(obfp, strlen(argv[i]) + 1, "%s", argv[i]);
    obfp += strlen(argv[i]);
    *obfp = 0;
    debug(1, "Command Line: \"%s\".", result);
  }

  if (sodium_init() < 0) {
    debug(1, "Can't initialise libsodium!");
  } else {
    debug(2, "libsodium initialised.");
  }

  // this code is based on
  // https://www.gnupg.org/documentation/manuals/gcrypt/Initializing-the-library.html

  /* Version check should be the very first call because it
    makes sure that important subsystems are initialized.
    #define NEED_LIBGCRYPT_VERSION to the minimum required version. */

#define NEED_LIBGCRYPT_VERSION "1.5.4"

  if (!gcry_check_version(NEED_LIBGCRYPT_VERSION)) {
    die("libgcrypt is too old (need %s, have %s).", NEED_LIBGCRYPT_VERSION,
        gcry_check_version(NULL));
  }

  /* Disable secure memory.  */
  gcry_control(GCRYCTL_DISABLE_SECMEM, 0);

  /* ... If required, other initialization goes here.  */

  /* Tell Libgcrypt that initialization has completed. */
  gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

  debug(2, "libgcrypt initialised.");


  debug(2, "Log Verbosity is %d.", debug_level());

  config.output = audio_get_output(config.output_name);
  if (!config.output) {
    die("the audio backend selected: \"%s\" is not supported. Either it is invalid, or support has "
        "not been included in this build of Shairport Sync.",
        config.output_name == NULL ? "<unspecified>" : config.output_name);
  }
  debug(1, "audio backend is \"%s\".", config.output_name);
  config.output->init(argc - audio_arg, argv + audio_arg);

  if (debug_level() <= 1) // keep FFmpeg stuff quiet unless verbosity is 2 or more
    av_log_set_level(AV_LOG_QUIET);

#if LIBAVUTIL_VERSION_MAJOR >= 57

  // default multichannel on
  {
    AVChannelLayout default_layout =
        AV_CHANNEL_LAYOUT_7POINT1; // big fat macro to initialise the default layout
    config.eight_channel_layout = default_layout.u.mask;
  }
  {
    AVChannelLayout default_layout =
        AV_CHANNEL_LAYOUT_5POINT1; // big fat macro to initialise the default layout
    config.six_channel_layout = default_layout.u.mask;
  }

  const char *str;

  if ((config.cfg != NULL) &&
      (config_lookup_string(config.cfg, "general.eight_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.eight_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // AVChannelLayout default_layout =
      //     AV_CHANNEL_LAYOUT_7POINT1; // big fat macro to initialise the default layout
      // config.eight_channel_layout = default_layout.u.mask;
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        if (channel_layout.nb_channels == 8) {
          config.eight_channel_layout = channel_layout.u.mask;
        } else {
          warn("the eight_channel_mode setting \"%s\" is a %u-channel layout. If a channel layout "
               "is "
               "given, it must be an 8-channel layout. eight_channel_mode is set to \"off\".",
               str, channel_layout.nb_channels);
        }
        av_channel_layout_uninit(&channel_layout);
      } else {
        warn("the eight_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
             "\"on\" or an eight-channel FFmpeg channel layout, e.g. \"7.1\". "
             "eight_channel_mode is set to \"off\".",
             str);
      }
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_string(config.cfg, "general.six_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.six_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // AVChannelLayout default_layout =
      //     AV_CHANNEL_LAYOUT_5POINT1; // big fat macro to initialise the default layout
      // config.six_channel_layout = default_layout.u.mask;
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        if (channel_layout.nb_channels == 6) {
          config.six_channel_layout = channel_layout.u.mask;
        } else {
          warn("the six_channel_mode setting \"%s\" is a %u-channel layout. If a channel layout is "
               "given, it must be a 6-channel layout. six_channel_mode is set to \"off\".",
               str, channel_layout.nb_channels);
        }
        av_channel_layout_uninit(&channel_layout);
      } else {
        warn("the six_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
             "\"on\" or a six-channel FFmpeg channel layout, e.g. \"5.1\". "
             "six_channel_mode is set to \"off\".",
             str);
      }
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.mixdown", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.mixdown_enable = 0; // 0 on initialisation
      debug(1, "mixdown disabled.");
    } else if (strcasecmp(str, "auto") == 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
      debug(1, "mixdown target: auto.");
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        config.mixdown_enable = 1;
        config.mixdown_channel_layout = channel_layout.u.mask;
        av_channel_layout_uninit(&channel_layout);
        debug(1, "mixdown target: \"%s\".", str);
      } else {
        warn("the mixdown setting \"%s\" is not recognised -- it should be \"off\" or \"auto\" or "
             "an "
             "FFmpeg channel layout, e.g. \"stereo\". the mixdown is set to \"auto\".",
             str);
        config.mixdown_enable = 1;
        config.mixdown_channel_layout = 0; // 0 means auto
      }
    }
  }
#else

  // default on
  config.eight_channel_layout = AV_CH_LAYOUT_7POINT1;
  config.six_channel_layout = AV_CH_LAYOUT_5POINT1;

  const char *str;

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.eight_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.eight_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // config.eight_channel_layout = AV_CH_LAYOUT_7POINT1;
    } else if (av_get_channel_layout(str) != 0) {
      if (av_get_channel_layout_nb_channels(av_get_channel_layout(str)) == 8) {
        config.eight_channel_layout = av_get_channel_layout(str);
      } else {
        warn("the eight_channel_mode setting \"%s\" is a %u channel layout. If a channel layout is "
             "given, it must be an 8-channel layout. eight_channel_mode is set to \"off\".",
             str, av_get_channel_layout_nb_channels(av_get_channel_layout(str)));
      }
    } else {
      warn("the eight_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
           "\"on\" or an 8-channel FFmpeg channel layout, e.g. \"7.1\". "
           "eight_channel_mode is set to \"off\".",
           str);
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.six_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.six_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // config.six_channel_layout = AV_CH_LAYOUT_5POINT1;
    } else if (av_get_channel_layout(str) != 0) {
      if (av_get_channel_layout_nb_channels(av_get_channel_layout(str)) == 6) {
        config.six_channel_layout = av_get_channel_layout(str);
      } else {
        warn("the six_channel_mode setting \"%s\" is a %u channel layout. If a channel layout is "
             "given, it must be a 6-channel layout. six_channel_mode is set to \"off\".",
             str, av_get_channel_layout_nb_channels(av_get_channel_layout(str)));
      }
    } else {
      warn("the six_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
           "\"on\" or a 6-channel FFmpeg channel layout, e.g. \"5.1\". "
           "six_channel_mode is set to \"off\".",
           str);
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.mixdown", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.mixdown_enable = 0; // 0 on initialisation
    } else if (strcasecmp(str, "auto") == 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
    } else if (av_get_channel_layout(str) != 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = av_get_channel_layout(str);
    } else {
      warn("the mixdown setting \"%s\" is not recognised -- it should be \"off\" or \"auto\" or an "
           "FFmpeg channel layout, e.g. \"stereo\". the mixdown is set to \"auto\".",
           str);
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
    }
  }
#endif

  if (config.cfg != NULL) {
    config_setting_t *output_channel_mapping_setting =
        config_lookup(config.cfg, "general.output_channel_mapping");
    if (output_channel_mapping_setting != NULL) {
      const char *sstr = config_setting_get_string(output_channel_mapping_setting);
      if (sstr != NULL) { // definitely a string
        if (strcasecmp(sstr, "auto") == 0) {
          config.output_channel_mapping_enable = 1; // this is the default anyway
          config.output_channel_map_size = 0;       // use the device's channel map
          debug(1, "device output channel map chosen");
        } else if ((strcasecmp(sstr, "off") == 0) || (strcasecmp(sstr, "no") == 0)) {
          config.output_channel_mapping_enable = 0; // no mapping
        } else {
          warn("the output_channel_mapping setting \"%s\" is not recognised -- it should be "
               "\"auto\", \"off\" or a "
               "bracketed comma-separated list of short channel names, e.g. (\"FL\", \"FR\", "
               "\"LFE\");",
               sstr);
        }
      } else {
        int i = 0;
        for (i = 0; i < config_setting_length(output_channel_mapping_setting); i++) {
          // is a list or array, so okay
          const char *channel_id =
              config_setting_get_string_elem(output_channel_mapping_setting, i);
          if (channel_id != NULL) { // definitely a string
            int found = 0;
            if (strcmp(channel_id, "--") == 0) {
              found = 1;
            } else {
#if LIBAVUTIL_VERSION_MAJOR >= 57
              const int buffer_size = 32;
              char buffer[buffer_size];
              enum AVChannel channel_index;
              for (channel_index = AV_CHAN_NONE;
                   ((channel_index < AV_CHAN_BOTTOM_FRONT_RIGHT) && (found == 0));
                   channel_index++) {
                found = av_channel_name(buffer, buffer_size, channel_index);
                if (found > 0) {
                  found = ((av_channel_name(buffer, buffer_size, channel_index) > 0) &&
                           (strcmp(channel_id, buffer) == 0));
                } else {
                  found = 0;
                }
              }
#else
              uint64_t channel_index;
              for (channel_index = 0; ((channel_index < 64) && (found == 0)); channel_index++) {
                found = ((av_get_channel_name(1 << channel_index) != NULL) &&
                         (strcmp(channel_id, av_get_channel_name(1 << channel_index)) == 0));
              }
#endif
            }
            if (found != 0) {
              config.output_channel_map[i] = strdup(channel_id);
              debug(2, "output channel %d is \"%s\".", i, config.output_channel_map[i]);
            } else {

              warn("during channel mapping, \"%s\" was not recognised as a channel name -- as a "
                   "result, output channel %d will be silent.",
                   channel_id, i);
              config.output_channel_map[i] = strdup("--");
            }
            config.output_channel_map_size++;
          }
        }
        if (config.output_channel_map_size == 0)
          warn("the output_channel_mapping setting was empty. No output channel mapping will be "
               "done.");
        else
          config.output_channel_mapping_enable = 1;
      }
    }
  }


  switch (config.endianness) {
  case SS_LITTLE_ENDIAN:
    debug(2, "The processor is running little-endian.");
    break;
  case SS_BIG_ENDIAN:
    debug(2, "The processor is running big-endian.");
    break;
  case SS_PDP_ENDIAN:
    debug(2, "The processor is running pdp-endian.");
    break;
  }

  /* Mess around with the latency options */
  // Basically, we expect the source to set the latency and add a fixed offset of 11025 frames to
  // it, which sounds right
  // If this latency is outside the max and min latensies that may be set by the source, clamp it to
  // fit.

  // If they specify a non-standard latency, we suggest the user to use the
  // audio_backend_latency_offset instead.

  if (config.userSuppliedLatency) {
    inform("The fixed latency setting is deprecated, as Shairport Sync gets the correct "
           "latency automatically from the source.");
    inform("Use the audio_backend_latency_offset_in_seconds setting "
           "instead to compensate for timing issues.");
    if ((config.userSuppliedLatency != 0) &&
        ((config.userSuppliedLatency < 4410) ||
         (config.userSuppliedLatency > BUFFER_FRAMES * 352 - 22050)))
      die("An out-of-range fixed latency has been specified. It must be between 4410 and %d (at "
          "44100 frames per second).",
          BUFFER_FRAMES * 352 - 22050);
  }
  const int option_print_level = 1;
  /* Print out options */
  debug(option_print_level, "disable_resend_requests is %s.",
        config.disable_resend_requests ? "on" : "off");
  debug(option_print_level,
        "diagnostic_drop_packet_fraction is %f. A value of 0.0 means no packets will be dropped "
        "deliberately.",
        config.diagnostic_drop_packet_fraction);
  debug(option_print_level, "statistics_requester status is %d.", config.statistics_requested);
  debug(option_print_level, "rtsp listening port is %d.", config.port);
  debug(option_print_level, "udp base port is %d.", config.udp_port_base);
  debug(option_print_level, "udp port range is %d.", config.udp_port_range);
  debug(option_print_level, "player name is \"%s\".", config.service_name);
  debug(option_print_level, "run_this_before_play_begins action is \"%s\".",
        strnull(config.cmd_start));
  debug(option_print_level, "run_this_after_play_ends action is \"%s\".", strnull(config.cmd_stop));
  debug(option_print_level, "wait-cmd status is %d.", config.cmd_blocking);
  debug(option_print_level, "run_this_before_play_begins may return output is %d.",
        config.cmd_start_returns_output);
  debug(option_print_level, "run_this_if_an_unfixable_error_is_detected action is \"%s\".",
        strnull(config.cmd_unfixable));
  debug(option_print_level, "run_this_before_entering_active_state action is  \"%s\".",
        strnull(config.cmd_active_start));
  debug(option_print_level, "run_this_after_exiting_active_state action is  \"%s\".",
        strnull(config.cmd_active_stop));
  debug(option_print_level, "active_state_timeout is  %f seconds.", config.active_state_timeout);
  debug(option_print_level, "mdns backend \"%s\".", strnull(config.mdns_name));
  debug(2, "userSuppliedLatency is %d.", config.userSuppliedLatency);
  debug(option_print_level, "interpolation setting is \"%s\".",
        config.packet_stuffing == ST_basic     ? "basic"
        : config.packet_stuffing == ST_vernier ? "vernier"
        : config.packet_stuffing == ST_soxr    ? "soxr"
                                               : "auto");
  debug(option_print_level, "interpolation soxr_delay_threshold is %d.",
        config.soxr_delay_threshold);
  debug(option_print_level, "resync time is %f seconds.", config.resync_threshold);
  debug(option_print_level, "allow a classic AirPlay session to be interrupted: \"%s\".",
        config.allow_session_interruption == 0 ? "no" : "yes");
  debug(option_print_level, "busy timeout time is %d.", config.timeout);
  debug(option_print_level, "drift tolerance is %f seconds.", config.tolerance);
  debug(option_print_level, "password is \"%s\".", strnull(config.password));
  debug(option_print_level, "default airplay volume is: %.6f.", config.default_airplay_volume);
  debug(option_print_level, "ignore_volume_control is %d.", config.ignore_volume_control);
  if (config.volume_max_db_set)
    debug(option_print_level, "volume_max_db is %d.", config.volume_max_db);
  else
    debug(option_print_level, "volume_max_db is not set");
  debug(option_print_level,
        "volume range in dB (zero means use the range specified by the mixer): %u.",
        config.volume_range_db);
  debug(option_print_level,
        "volume_range_combined_hardware_priority (1 means hardware mixer attenuation is used "
        "first) is %d.",
        config.volume_range_hw_priority);
  debug(option_print_level,
        "playback_mode is %d (0-stereo, 1-mono, 1-reverse_stereo, 2-both_left, 3-both_right).",
        config.playback_mode);
  debug(option_print_level, "disable_synchronization is %d.", config.no_sync);
  debug(option_print_level, "use_mmap_if_available is %d.", config.no_mmap ? 0 : 1);
  debug(option_print_level, "output_format automatic selection is %sabled.",
        config.output_format_auto_requested ? "en" : "dis");
  // if (config.output_format_auto_requested == 0)
  //   debug(option_print_level, "output_format is \"%s\".",
  //         sps_format_description_string(config.current_output_configuration->format));
  debug(option_print_level, "output_rate automatic selection is %sabled.",
        config.output_rate_auto_requested ? "en" : "dis");
  // if (config.output_rate_auto_requested == 0)
  //   debug(option_print_level, "output_rate is %d.", config.current_output_configuration->rate);
  debug(option_print_level, "audio backend desired buffer length is %f seconds.",
        config.audio_backend_buffer_desired_length);
  debug(option_print_level,
        "audio_backend_buffer_interpolation_threshold_in_seconds is %f seconds.",
        config.audio_backend_buffer_interpolation_threshold_in_seconds);
  debug(option_print_level, "audio backend latency offset is %f seconds.",
        config.audio_backend_latency_offset);
  if (config.audio_backend_silent_lead_in_time_auto == 1)
    debug(option_print_level, "audio backend silence lead-in time is \"auto\".");
  else
    debug(option_print_level, "audio backend silence lead-in time is %f seconds.",
          config.audio_backend_silent_lead_in_time);
  debug(option_print_level, "zeroconf regtype is \"%s\".", config.regtype);
  debug(option_print_level,
        "decoders_supported bit field is %d (1 == hammerton, 2 == apple, 4 == ffmpeg).",
        config.decoders_supported);
  debug(option_print_level, "decoder_in_use is %d.", config.decoder_in_use);
  debug(option_print_level, "alsa_use_hardware_mute is %d.", config.alsa_use_hardware_mute);
  if (config.interface)
    debug(option_print_level, "mdns service interface \"%s\" requested.", config.interface);
  else
    debug(option_print_level, "no special mdns service interface was requested.");
  char *realConfigPath = realpath(config.configfile, NULL);
  if (realConfigPath) {
    debug(option_print_level, "configuration file name \"%s\" resolves to \"%s\".",
          config.configfile, realConfigPath);
    free(realConfigPath);
  } else {
    debug(option_print_level, "configuration file name \"%s\" can not be resolved.",
          config.configfile);
  }

  debug(option_print_level, "loudness_enabled is %s.",
        config.loudness_enabled != 0 ? "true" : "false");
  debug(option_print_level, "loudness reference level is %f", config.loudness_reference_volume_db);


  debug(2, "LIBAVUTIL_VERSION_MAJOR is %d", LIBAVUTIL_VERSION_MAJOR);

#if LIBAVUTIL_VERSION_MAJOR >= 57
  unsigned int cc;
  for (cc = 1; cc <= 8; cc++) {
    AVChannelLayout output_channel_layout;
    av_channel_layout_default(&output_channel_layout, cc);
    char chLayoutDescription[1024];
    int sts = av_channel_layout_describe(&output_channel_layout, chLayoutDescription,
                                         sizeof(chLayoutDescription));
    if (sts >= 0) {
      int idx;
      char channel_map[1024];
      channel_map[0] = '\0';
      for (idx = 0; idx < output_channel_layout.nb_channels; idx++) {
        enum AVChannel av = av_channel_layout_channel_from_index(&output_channel_layout, idx);
        char chName[64];
        int cts = av_channel_name(chName, sizeof(chName), av);
        if (cts >= 0) {
          if (idx != 0)
            strncat(channel_map, " ", sizeof(channel_map) - 1);
          strncat(channel_map, chName, sizeof(channel_map) - 1 - strlen(channel_map));
          debug(4, "Channel %d: \"%s\"", idx, chName);
        } else {
          debug(1, "Insufficient space for the name of channel %d.", idx);
        }
      }
      if (cc == 1) {
        debug(2, "Default layout for one channel: \"%s\", channel map: \"%s\".",
              chLayoutDescription, channel_map);
      } else {
        debug(2, "Default layout for %u channels: \"%s\", channel map: \"%s\".", cc,
              chLayoutDescription, channel_map);
      }
    } else {
      debug(1, "Insufficient space for the description of the default layout for %u channels.", cc);
    }
    av_channel_layout_uninit(&output_channel_layout);
  }
#endif


  // In AirPlay 2 mode, the AP1 prefix is the same as the device ID less the colons
  // and has already been calculated.

  // In AirPlay 1 mode, the AP1 prefix is calculated by hashing the service name.

  if (config.service_type != APST_airplay2) {
    uint8_t ap_md5[16];
    // debug(1, "size of hw_addr is %u.", sizeof(config.hw_addr));
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
    EVP_DigestUpdate(mdctx, config.service_name, strlen(config.service_name));
    EVP_DigestUpdate(mdctx, config.hw_addr, sizeof(config.hw_addr));
    unsigned int md5_digest_len = EVP_MD_size(EVP_md5());
    EVP_DigestFinal_ex(mdctx, ap_md5, &md5_digest_len);
    EVP_MD_CTX_free(mdctx);


    memcpy(config.ap1_prefix, ap_md5, sizeof(config.ap1_prefix));

  }







  activity_monitor_start();
  debug(4, "create an RTSP listener");
  // note: the Avahi Threaded Poll thread will be named after whatever name you use here too, so
  // you'll see two threads named "listener" or whatever...
  named_pthread_create(&rtsp_listener_thread, NULL, &rtsp_listen_loop, NULL, "listener");
  atexit(exit_rtsp_listener);
  
  // wait forever...
  while (1) {
    usleep(1000000);
  }
  return 0;
}
