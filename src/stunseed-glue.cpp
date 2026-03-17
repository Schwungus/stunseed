// datachannel-wasm only provides a C++ API. since we're using a C++ linker regardless of the target
// platform (the native libdatachannel is mostly C++ as well, with only an extern "C" compatibility
// layer), i've decided to stuff all of this interoperable C++ glue into its own file.

#include <memory>
#include <string>
#include <variant>

#include <rtc/candidate.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>

#include <yyjson.h>

#include "stunseed.h"

static enum stunseed_mode_t {
	STUNSEED_MODE_IDLE,
	STUNSEED_MODE_HOST,
	STUNSEED_MODE_JOIN,
} stunseed_mode = STUNSEED_MODE_IDLE;

static rtc::Configuration stunseed_rtc_config;
static stunseed_peer_info stunseed_peers[STUNSEED_MAX_PEERS] = {0};
static std::unique_ptr<rtc::WebSocket> stunseed_tracker_sock = nullptr;

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
	stunseed_log_level log_level = stunseed_log_level::STUNSEED_LOG_INFO;

	if (level != rtc::LogLevel::Info)
		log_level = stunseed_log_level::STUNSEED_LOG_WARN;

	stunseed_log(log_level, "%s", line.c_str());
}

typedef struct {
	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::DataChannel> dc;
	stunseed_peer_info* backptr;
} stunseed_glue;

extern "C" void stunseed_maybe_announce();

static void stunseed_glue_create_pc(stunseed_glue* glue) {
	glue->pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);

	glue->pc->onLocalDescription([glue](const rtc::Description& description) {
		if (glue->backptr->sdp)
			return;
		std::string sdp = description;
		stunseed_warn("DAMN %s", sdp.c_str());
		glue->backptr->sdp = (char*)malloc(sdp.length() + 1);
		memcpy(glue->backptr->sdp, sdp.c_str(), sdp.length() + 1);
		stunseed_maybe_announce();
	});

	/*glue->pc->onLocalCandidate(
		[](const rtc::Candidate& candidate) { stunseed_announce(std::string(candidate).c_str()); });*/

	glue->pc->onStateChange([](rtc::PeerConnection::State state) {
		if (state >= rtc::PeerConnection::State::Disconnected)
			stunseed_warn("DAMN IT"); // TODO: handle disconnection
	});
}

extern "C" void stunseed_kill_tracker_sock() {
	if (stunseed_tracker_sock)
		stunseed_tracker_sock->close();
}

static void stunseed_nuke_peer(void* raw) {
	auto peer = (stunseed_peer_info*)raw;

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
	stunseed_mode = (stunseed_mode_t)mode;

	stunseed_kill_tracker_sock();
	stunseed_tracker_sock = std::make_unique<rtc::WebSocket>();
	stunseed_tracker_sock->open(STUNSEED_DEFAULT_TRACKER);

	stunseed_tracker_sock->onMessage([](const auto& msg) {
		if (std::holds_alternative<std::string>(msg)) {
			const auto& s = std::get<std::string>(msg);
			stunseed_warn("recv: %s", s.c_str());
		}
	});

	for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
		stunseed_nuke_peer(stunseed_peers + i);
}

static int stunseed_described_peer_count() {
	int count = 0;
	while (count < STUNSEED_MAX_PEERS && stunseed_peers[count].sdp)
		count += 1;
	return count;
}

extern "C" int stunseed_peer_count() {
	int count = 0;
	while (count < STUNSEED_MAX_PEERS && stunseed_peers[count].glue)
		count += 1;
	return count;
}

extern "C" void stunseed_maybe_announce() {
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
	if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
		stunseed_tracker_sock->send(std::string(payload, len));
	free(payload), payload = NULL;
	yyjson_mut_doc_free(doc), doc = NULL;
}

void stunseed_nuke_glue(void* raw) {
	auto glue = reinterpret_cast<stunseed_glue*>(raw);
	delete glue;
}

extern "C" void stunseed_glue_set_stun_server() {
	stunseed_rtc_config.iceServers.emplace_back(STUNSEED_DEFAULT_STUN);
}

extern "C" void stunseed_glue_set_rtc_logger() {
	rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_glue_setup_dc(const std::shared_ptr<rtc::DataChannel>& dc) {
	dc->onOpen([dc]() {
		// TODO: use properly.
		dc->send("hi vru!");
	});

	dc->onClosed([]() {
		// TODO: use properly.
	});

	dc->onMessage([dc](const auto& msg) {
		if (!std::holds_alternative<std::string>(msg))
			return;
		const auto s = std::get<std::string>(msg);
		stunseed_info("RECEIVED: %s", s.c_str());
		dc->send(s);
	});
}

extern "C" void stunseed_glue_create_offer(stunseed_peer_info* peer) {
	auto glue = new stunseed_glue;
	peer->glue = glue, glue->backptr = peer;

	stunseed_glue_create_pc(glue);
	glue->dc = glue->pc->createDataChannel("bruh");
	stunseed_glue_setup_dc(glue->dc);
}

extern "C" void stunseed_glue_create_answer(stunseed_peer_info* peer) {
	auto glue = new stunseed_glue;
	peer->glue = glue, glue->backptr = peer;

	stunseed_glue_create_pc(glue);
	glue->pc->onDataChannel([glue](const auto& dc) {
		glue->dc = dc;
		stunseed_glue_setup_dc(glue->dc);
	});
}

static void stunseed_create_offers(int count) {
	for (int i = 0; i < count; i++)
		stunseed_glue_create_offer(stunseed_peers + i);
}

extern "C" void stunseed_host(const char* secret, int count) {
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
	// stunseed_create_offers(count);
}

extern "C" void stunseed_join(const char* secret) {
	stunseed_prepare(secret, STUNSEED_MODE_JOIN);
	// stunseed_create_offers(STUNSEED_MAX_PEERS);
}

extern "C" void stunseed_echo() {
	if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
		stunseed_tracker_sock->send("damn bro");
}
