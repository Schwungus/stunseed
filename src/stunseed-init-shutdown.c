#include <stdbool.h>
#include <stdlib.h>

#include "stunseed.h"

void stunseed_init() {
    static bool stunseed_init_done = false;
    if (stunseed_init_done)
        return;
    stunseed_init_done = true;

    extern void stunseed_glue_init();
    stunseed_glue_init();

    stunseed_set_channel_count(1);
    srand(stunseed_time_ns());

    extern void stunseed_glue_set_rtc_logger();
    stunseed_glue_set_rtc_logger();

    stunseed_info("welcome to stunseed! v%s", STUNSEED_VERSION);
}

void stunseed_shutdown() {
    stunseed_disconnect();

    extern void stunseed_glue_cleanup();
    stunseed_glue_cleanup();

    stunseed_info("stunseed out!");
}
