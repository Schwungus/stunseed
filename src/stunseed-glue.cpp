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

struct stunseed_glue {
	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::DataChannel> dc;
	stunseed_peer_info c;

	~stunseed_glue() {
		if (c.sdp)
			free(c.sdp), c.sdp = NULL;
	}
};

static enum stunseed_mode_t {
	STUNSEED_MODE_IDLE,
	STUNSEED_MODE_HOST,
	STUNSEED_MODE_JOIN,
} stunseed_mode = STUNSEED_MODE_IDLE;

static rtc::Configuration stunseed_rtc_config;
static std::vector<stunseed_glue> stunseed_peers;
static std::unique_ptr<rtc::WebSocket> stunseed_tracker_sock = nullptr;
static stunseed_webtorrent_id stunseed_lobby_id = {0}, stunseed_peer_id = {0};

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
	stunseed_log_level log_level = stunseed_log_level::STUNSEED_LOG_INFO;

	if (level != rtc::LogLevel::Info)
		log_level = stunseed_log_level::STUNSEED_LOG_WARN;

	stunseed_log(log_level, "%s", line.c_str());
}

static int stunseed_sdp_ready_count() {
	int count = 0;
	while (count < stunseed_peers.size() && stunseed_peers[count].c.sdp)
		count += 1;
	return count;
}

extern "C" int stunseed_peer_count() {
	return (int)stunseed_peers.size();
}

extern "C" const char* stunseed_get_our_id() {
	return stunseed_peer_id;
}

static void stunseed_maybe_announce() {
	if (!stunseed_peer_count() || stunseed_sdp_ready_count() != stunseed_peer_count())
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
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "info_hash"), yyjson_mut_str(doc, stunseed_lobby_id));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "peer_id"), yyjson_mut_str(doc, stunseed_peer_id));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "downloaded"), yyjson_mut_int(doc, 0));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "left"), yyjson_mut_int(doc, 1000));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "uploaded"), yyjson_mut_int(doc, 0));
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "numwant"), yyjson_mut_int(doc, STUNSEED_MAX_PEERS));

	yyjson_mut_val* offers = yyjson_mut_arr(doc);
	for (int i = 0; i < stunseed_peer_count(); i++) {
		yyjson_mut_val* offer = yyjson_mut_obj(doc);
		yyjson_mut_obj_add(offer, yyjson_mut_str(doc, "offer"), yyjson_mut_str(doc, stunseed_peers[i].c.sdp));
		yyjson_mut_obj_add(offer, yyjson_mut_str(doc, "offer_id"), yyjson_mut_str(doc, ""));
		yyjson_mut_arr_append(offers, offer);
	}
	yyjson_mut_obj_add(root, yyjson_mut_str(doc, "offers"), offers);

	yyjson_mut_doc_set_root(doc, root);

	size_t len = 0;
	char* payload = yyjson_mut_write(doc, 0, &len);
	if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
		stunseed_tracker_sock->send(std::string(payload, len));
	else
		stunseed_warn("gosh darn it");
	free(payload), payload = NULL;
	yyjson_mut_doc_free(doc), doc = NULL;
}

extern "C" void stunseed_update() {
	static uint64_t last_update = 0;
	const uint64_t now = stunseed_time_ns();

	if (!last_update || now - last_update > 100000000) { // 100ms
		stunseed_maybe_announce();
		last_update = now;
	}
}

extern "C" void stunseed_kill_tracker_sock() {
	if (stunseed_tracker_sock)
		stunseed_tracker_sock->close();
}

static void stunseed_prepare(int mode) {
	stunseed_init();
	stunseed_mode = (stunseed_mode_t)mode;

	stunseed_kill_tracker_sock();
	stunseed_tracker_sock = std::make_unique<rtc::WebSocket>();
	stunseed_tracker_sock->open(STUNSEED_DEFAULT_TRACKER);

	stunseed_tracker_sock->onClosed([]() {
		// TODO: handle.
	});

	stunseed_tracker_sock->onMessage([](const auto& msg) {
		if (std::holds_alternative<std::string>(msg)) {
			const auto& s = std::get<std::string>(msg);
			stunseed_warn("recv: %s", s.c_str());
		} else {
			stunseed_warn("SHIT RECV");
		}
	});

	stunseed_peers.clear();
	stunseed_generate_webtorrent_id(stunseed_peer_id);
	stunseed_info("we are ID=%s", stunseed_peer_id);
}

extern "C" void stunseed_glue_set_stun_server() {
	stunseed_rtc_config.iceServers.emplace_back(STUNSEED_DEFAULT_STUN);
}

extern "C" void stunseed_glue_set_rtc_logger() {
	rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_create_offers() {
	stunseed_peers.clear();

	for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
		stunseed_peers.emplace_back();

	for (int i = 0; i < STUNSEED_MAX_PEERS; i++) {
		auto& peer = stunseed_peers[i];
		stunseed_generate_webtorrent_id(peer.c.offer_id);

		peer.pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);

		peer.pc->onLocalDescription([&](const rtc::Description& description) {
			if (peer.c.sdp)
				return;
			std::string sdp = description;
			peer.c.sdp = (char*)malloc(sdp.length() + 1);
			memcpy(peer.c.sdp, sdp.c_str(), sdp.length() + 1);
			stunseed_maybe_announce();
		});

		/*glue->pc->onLocalCandidate(
			[](const rtc::Candidate& candidate) { stunseed_announce(std::string(candidate).c_str()); });*/

		peer.pc->onStateChange([](rtc::PeerConnection::State state) {
			if (state >= rtc::PeerConnection::State::Disconnected)
				(void)0; // TODO: handle disconnection
		});

		peer.dc = peer.pc->createDataChannel("bruh");

		peer.dc->onOpen([&]() {
			// TODO: use properly.
			peer.dc->send("hi vru!");
		});

		peer.dc->onClosed([]() {
			// TODO: use properly.
		});

		peer.dc->onMessage([&](const auto& msg) {
			if (!std::holds_alternative<std::string>(msg))
				return;
			const auto s = std::get<std::string>(msg);
			stunseed_warn("RECEIVED: %s", s.c_str());
			peer.dc->send(s);
		});
	}
}

#define LOBBY_ID "12345678901234567890"

extern "C" void stunseed_host(int count) {
	stunseed_prepare(STUNSEED_MODE_HOST);
	// stunseed_generate_webtorrent_id(stunseed_lobby_id);
	memcpy(stunseed_lobby_id, LOBBY_ID, sizeof(stunseed_lobby_id));

	if (count > STUNSEED_MAX_PEERS) {
		count = STUNSEED_MAX_PEERS;
		stunseed_warn("requested %d peers > %d max", count, STUNSEED_MAX_PEERS);
	}

	if (count < 1) {
		count = 1;
		stunseed_warn("requested <1 peers", count);
	}

	stunseed_info("hosting. %d peers max", count);
	stunseed_create_offers();
}

extern "C" void stunseed_join(stunseed_webtorrent_id id) {
	(void)id;
	stunseed_prepare(STUNSEED_MODE_JOIN);
	memcpy(stunseed_lobby_id, LOBBY_ID, sizeof(stunseed_lobby_id));

	stunseed_info("joining...");
	stunseed_create_offers();
}

extern "C" void stunseed_echo() {
	if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
		stunseed_tracker_sock->send("damn bro");
}
