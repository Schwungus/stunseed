#include <stddef.h>
#include <string.h>

#include <bq_websocket.h>
#include <bq_websocket_platform.h>

#include <yyjson.h>

#include "stunseed.h"

static enum {
	STUNSEED_MODE_IDLE,
	STUNSEED_MODE_HOST,
	STUNSEED_MODE_JOIN,
} stunseed_mode = STUNSEED_MODE_IDLE;

static stunseed_peer_info stunseed_peers[STUNSEED_MAX_PEERS] = {0};
static bqws_socket* stunseed_tracker_sock = NULL;

int stunseed_peer_count() {
	int count = 0;
	while (count < STUNSEED_MAX_PEERS && stunseed_peers[count].glue)
		count += 1;
	return count;
}

static int stunseed_described_peer_count() {
	int count = 0;
	while (count < STUNSEED_MAX_PEERS && stunseed_peers[count].sdp)
		count += 1;
	return count;
}

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

void stunseed_maybe_announce() {
	stunseed_warn("AUGH %d vs %d", stunseed_described_peer_count(), stunseed_peer_count());
	if (stunseed_described_peer_count() != stunseed_peer_count())
		return;

	/*
from chatgpt:

const announceMsg = {
	action: 'announce',
	info_hash: '...', // 20-byte binary/hex string
	peer_id: '-WW0001-abcdefghij', // Your unique 20-byte ID
	downloaded: 0,
	left: 1000,
	uploaded: 0,
	numwant: 50 // Number of peers requested
	};
socket.send(JSON.stringify(announceMsg));
	*/

	yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* root = yyjson_mut_obj(doc);

	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "action"), yyjson_mut_str(doc, "announce"));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "info_hash"), yyjson_mut_str(doc, "12345678901234567890"));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "peer_id"), yyjson_mut_str(doc, "12345678901234567890"));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "downloaded"), yyjson_mut_int(doc, 0));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "left"), yyjson_mut_int(doc, 1000));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "uploaded"), yyjson_mut_int(doc, 0));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "numwant"), yyjson_mut_int(doc, STUNSEED_MAX_PEERS));

	yyjson_mut_val* offers = yyjson_mut_arr(doc);
	for (int i = 0; i < stunseed_peer_count(); i++)
		yyjson_mut_arr_append(offers, yyjson_mut_str(doc, stunseed_peers[i].sdp));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "offers"), offers);

	yyjson_mut_doc_set_root(doc, root);

	size_t len = 0;
	char* payload = yyjson_mut_write(doc, 0, &len);
	stunseed_info("SHIT %.*s", len, payload);
	bqws_send(stunseed_tracker_sock, BQWS_MSG_TEXT, payload, len);
	free(payload), payload = NULL;
	yyjson_mut_doc_free(doc), doc = NULL;
}

static void stunseed_nuke_peer(void* raw) {
	stunseed_peer_info* peer = raw;

	if (peer->glue) {
		extern void stunseed_nuke_glue(void*);
		stunseed_nuke_glue(peer->glue);
		peer->glue = NULL;
	}

	if (peer->sdp)
		free(peer->sdp), peer->sdp = NULL;

	memset(peer, 0, sizeof(*peer));
}

static void stunseed_prepare(const char* secret, int mode) {
	(void)secret;

	stunseed_init();
	stunseed_mode = mode;

	stunseed_kill_tracker_sock();
	stunseed_tracker_sock = bqws_pt_connect(STUNSEED_DEFAULT_TRACKER, NULL, NULL, NULL);
	log_bqws_error();

	for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
		stunseed_nuke_peer(stunseed_peers + i);
}

static void stunseed_create_offers(int count) {
	for (int i = 0; i < count; i++) {
		extern void stunseed_glue_create_offer(stunseed_peer_info*);
		stunseed_glue_create_offer(stunseed_peers + i);
	}
}

void stunseed_host(const char* secret, int count) {
	stunseed_prepare(secret, STUNSEED_MODE_HOST);

	if (count > STUNSEED_MAX_PEERS) {
		count = STUNSEED_MAX_PEERS;
		stunseed_warn("requested %d peers > %d max", count, STUNSEED_MAX_PEERS);
	}

	if (count < 1) {
		count = 1;
		stunseed_warn("requested <1 peers", count);
	}

	stunseed_info("%d peers max", count);
	stunseed_create_offers(count);
}

void stunseed_join(const char* secret) {
	stunseed_prepare(secret, STUNSEED_MODE_JOIN);
	stunseed_create_offers(STUNSEED_MAX_PEERS);
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
