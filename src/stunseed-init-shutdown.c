#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "stunseed.h"

static bool stunseed_init_done = false;
static char stunseed_our_id[sizeof(stunseed_peer_id) + 1] = {0};

const char* stunseed_get_our_id() {
	stunseed_init();
	return stunseed_our_id;
}

void stunseed_init() {
	if (stunseed_init_done)
		return;
	stunseed_init_done = true;

	// future me, please take note of this...
	struct timespec ts = {0};
	timespec_get(&ts, TIME_UTC);
	srand(ts.tv_sec * 1000000000 + ts.tv_nsec);

	for (int i = 0; i < sizeof(stunseed_peer_id); i++)
		stunseed_our_id[i] = (char)('A' + (rand() % 26));

	extern void stunseed_glue_set_rtc_logger();
	stunseed_glue_set_rtc_logger();

	extern void stunseed_glue_set_stun_server();
	stunseed_glue_set_stun_server();

	stunseed_info("welcome to stunseed! you are ID=%s", stunseed_our_id);
}

void stunseed_shutdown() {
	extern void stunseed_kill_tracker_sock();
	stunseed_kill_tracker_sock();

	stunseed_info("stunseed out!");
}
