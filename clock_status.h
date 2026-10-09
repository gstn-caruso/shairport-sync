#pragma once

typedef enum {
  clock_no_anchor_info,
  clock_ok,
  clock_service_unavailable,
  clock_access_error,
  clock_data_unavailable,
  clock_no_master,
  clock_version_mismatch,
  clock_not_synchronised,
  clock_not_valid,
  clock_not_ready,
} clock_status_t;
