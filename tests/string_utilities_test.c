#include "common.h"
#include "utilities/string_utilities.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *test_hostname = "receiver.local";

int gethostname(char *name, size_t length) {
  assert(strlen(test_hostname) < length);
  strcpy(name, test_hostname);
  return 0;
}

static void assert_owned_text(char *actual, const char *expected) {
  assert(actual != NULL);
  assert(strcmp(actual, expected) == 0);
  free(actual);
}

static void check_replacement(void) {
  assert_owned_text(str_replace("stable", "", ""), "stable");
  assert_owned_text(str_replace("stable", "", "x"), "stable");
  assert(str_replace(NULL, "x", "y") == NULL);
  assert_owned_text(str_replace("plain", "absent", "x"), "plain");
  assert_owned_text(str_replace("ab ab ab", "ab", "longer"), "longer longer longer");
  assert_owned_text(str_replace("aaaaa", "aa", "b"), "bba");
  assert_owned_text(str_replace("xx", "x", "xx"), "xxxx");
  assert_owned_text(str_replace("a--b--", "--", ""), "ab");
  assert_owned_text(str_replace("", "x", "y"), "");
  char original[] = "original";
  char *copy = str_replace(original, NULL, "x");
  assert(copy != original);
  assert_owned_text(copy, original);
  copy = str_replace(original, "x", NULL);
  assert(copy != original);
  assert_owned_text(copy, original);
}

static void check_truncation(void) {
  assert(append_truncated(NULL, "!", 5) == NULL);
  assert(append_truncated("base", NULL, 5) == NULL);
  assert_owned_text(append_truncated("base", "!", 5), "base!");
  assert_owned_text(append_truncated("base", "!", 8), "base!");
  assert_owned_text(append_truncated("abcdef", "!", 6), "ab...!");
  assert_owned_text(append_truncated("abcdef", "!", 4), "...!");
  assert(append_truncated("abcdef", "!", 3) == NULL);
  assert_owned_text(append_truncated("", "", 0), "");
  assert_owned_text(append_truncated("a\xc3\xa9" "bcdef", "!", 6), "a...!");
  assert_owned_text(append_truncated("a\xc3\xa9" "bcdef", "!", 7), "a\xc3\xa9...!");
  assert_owned_text(append_truncated("a\xe2\x82\xac" "bcdef", "!", 7), "a...!");
  assert_owned_text(append_truncated("a\xf0\x9f\x8e\xb5" "bcdef", "!", 8), "a...!");
}

static void check_service_names(void) {
  assert_owned_text(service_name(NULL), "Receiver");
  assert_owned_text(service_name("%h / %H / %h"), "receiver / Receiver / receiver");
  assert_owned_text(service_name("%v"), PACKAGE_VERSION);
  char *version = get_version_string();
  char *expected = append_truncated(version, "", 50);
  assert_owned_text(service_name("%V"), expected);
  free(expected);
  free(version);
  assert_owned_text(service_name("%unknown"), "%unknown");
  assert_owned_text(service_name(""), "");
  test_hostname = "music.room.local";
  assert_owned_text(service_name(NULL), "Music.room");
  test_hostname = "7receiver";
  assert_owned_text(service_name(NULL), "7receiver");
  test_hostname = "%H-%v.local";
  assert_owned_text(service_name("%h"), "%H-" PACKAGE_VERSION "-" PACKAGE_VERSION);
  test_hostname = "receiver.local";
  char long_name[60];
  memset(long_name, 'a', sizeof(long_name));
  long_name[59] = '\0';
  char truncated[51];
  memset(truncated, 'a', 47);
  strcpy(truncated + 47, "...");
  assert_owned_text(service_name(long_name), truncated);
  long_name[46] = '\xe2';
  long_name[47] = '\x82';
  long_name[48] = '\xac';
  strcpy(truncated + 46, "...");
  assert_owned_text(service_name(long_name), truncated);
}

int main(void) {
  check_replacement();
  check_truncation();
  check_service_names();
  return 0;
}
