#pragma once

#include "activity_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct activity_state activity_state;
enum activity_effect { activity_no_effect, activity_activate, activity_deactivate };
enum activity_wait { activity_wait_signal, activity_begin_timeout, activity_wait_deadline };

activity_state *activity_state_instance(void);
void activity_state_reset(activity_state *activity);
enum am_state activity_state_status(const activity_state *activity);
enum activity_effect activity_state_signify(activity_state *activity, int active, double timeout);
enum activity_wait activity_state_advance(activity_state *activity);
enum activity_effect activity_state_timeout_expired(activity_state *activity);
enum activity_effect activity_state_stop(activity_state *activity);
enum activity_effect activity_state_prepare_stop(const activity_state *activity);

#ifdef __cplusplus
}
#endif
