#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "stunseed.h"

static bool stunseed_init_done = false;

void stunseed_init() {
	if (stunseed_init_done)
		return;
	stunseed_init_done = true;

	// future me, please take note of this...
	struct timespec ts = {0};
	timespec_get(&ts, TIME_UTC);
	srand(ts.tv_sec * 1000000000 + ts.tv_nsec);

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
