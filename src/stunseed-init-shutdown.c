#include <stdbool.h>
#include <stdlib.h>

#include "stunseed.h"

static bool stunseed_init_done = false;

void stunseed_init() {
    if (stunseed_init_done)
        return;
    stunseed_init_done = true;

    srand(stunseed_time_ns());

    extern void stunseed_glue_set_rtc_logger();
    stunseed_glue_set_rtc_logger();

    extern void stunseed_glue_set_stun_server();
    stunseed_glue_set_stun_server();

    stunseed_info("welcome to stunseed!");
}

void stunseed_shutdown() {
    extern void stunseed_kill_tracker_sock();
    stunseed_kill_tracker_sock();

    stunseed_info("stunseed out!");
}
