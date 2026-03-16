#include <string.h>

#include <bq_websocket.h>
#include <bq_websocket_platform.h>

#include <S_tructures.h>

#include "stunseed.h"

static enum {
	STUNSEED_MODE_IDLE,
	STUNSEED_MODE_HOST,
	STUNSEED_MODE_JOIN,
} stunseed_mode = STUNSEED_MODE_IDLE;

static StTinyMap* stunseed_peers = NULL;
static int stunseed_max_peers = 1;

typedef struct {
	int peer_connection, data_channel;
	char offer[32];
} stunseed_peer_info;

static bqws_socket* stunseed_tracker_sock = NULL;

void stunseed_kill_tracker_sock() {
	bqws_free_socket(stunseed_tracker_sock);
	stunseed_tracker_sock = NULL;
}

static void log_bqws_error_fr(const char* file, int line) {
	bqws_pt_error error = {0};
	if (bqws_pt_get_error(&error)) {
		const char* type = bqws_pt_error_type_str(error.type);
		file = stunseed_basename(file);
		stunseed_warn("[%s:%d] %s: %d", file, line, error.function, error.data);
		bqws_pt_clear_error();
	}
}

#define log_bqws_error() log_bqws_error_fr(__FILE__, __LINE__)

static void stunseed_prepare(const char* secret, int mode) {
	stunseed_init();
	stunseed_mode = mode;

	FreeTinyMap(stunseed_peers);
	stunseed_peers = NewTinyMap();

	stunseed_kill_tracker_sock();
	stunseed_tracker_sock = bqws_pt_connect(STUNSEED_DEFAULT_TRACKER, NULL, NULL, NULL);
	log_bqws_error();
}

/* static void stunseed_mangle_lobby_secret(stunseed_peer_info* peer, const char* secret) {
	snprintf(peer->offer, sizeof(peer->offer), "STUNSEED_%s", secret);
} */

void stunseed_host(const char* secret, int count) {
	stunseed_prepare(secret, STUNSEED_MODE_HOST);
	stunseed_set_max_peers(count);

	stunseed_peer_info peer = {0};
	StMapPut(stunseed_peers, StStrKey(stunseed_get_our_id()), &peer, sizeof(peer));
}

void stunseed_join(const char* secret) {
	stunseed_prepare(secret, STUNSEED_MODE_JOIN);

	stunseed_peer_info peer = {0};
	StMapPut(stunseed_peers, StStrKey(stunseed_get_our_id()), &peer, sizeof(peer));
}

void stunseed_set_max_peers(int count) {
	if (count > STUNSEED_MAX_PEERS) {
		count = STUNSEED_MAX_PEERS;
		stunseed_warn("requested %d peers > %d max", count, STUNSEED_MAX_PEERS);
	}

	if (count < 1) {
		count = 1;
		stunseed_warn("requested <1 peers", count);
	}

	stunseed_info("%d peers max", count);
	stunseed_max_peers = count;
}

void stunseed_echo() {
	const char* s = "damn bro";
	if (stunseed_tracker_sock)
		bqws_send(stunseed_tracker_sock, BQWS_MSG_TEXT, s, strlen(s));
}

void stunseed_update() {
	if (stunseed_tracker_sock)
		bqws_update(stunseed_tracker_sock);
	log_bqws_error();

	bqws_msg* msg = NULL;
	while (stunseed_tracker_sock && (msg = bqws_recv(stunseed_tracker_sock))) {
		if (msg->type != BQWS_MSG_TEXT)
			goto skip;

		stunseed_info("%.*s", msg->size, msg->data);

	skip:
		bqws_free_msg(msg);
	}

	log_bqws_error();
}
