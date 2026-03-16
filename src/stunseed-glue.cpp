// datachannel-wasm only provides a C++ API. since we're using a C++ linker regardless of the target
// platform (the native libdatachannel is mostly C++ as well, with only an extern "C" compatibility
// layer), i've decided to stuff all of this interoperable C++ glue into its own file.

#include <rtc/candidate.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/rtc.hpp>

#include <memory>
#include <variant>

#include "stunseed.h"

static rtc::Configuration stunseed_rtc_config;

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

extern "C" {
void stunseed_nuke_glue(void* raw) {
	auto glue = reinterpret_cast<stunseed_glue*>(raw);
	delete glue;
}

void stunseed_glue_set_stun_server() {
	stunseed_rtc_config.iceServers.emplace_back(STUNSEED_DEFAULT_STUN);
}

void stunseed_glue_set_rtc_logger() {
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

void stunseed_glue_create_offer(stunseed_peer_info* peer) {
	auto glue = new stunseed_glue;
	peer->glue = glue, glue->backptr = peer;

	stunseed_glue_create_pc(glue);
	glue->dc = glue->pc->createDataChannel("bruh");
	stunseed_glue_setup_dc(glue->dc);
}

void stunseed_glue_create_answer(stunseed_peer_info* peer) {
	auto glue = new stunseed_glue;
	peer->glue = glue, glue->backptr = peer;

	stunseed_glue_create_pc(glue);
	glue->pc->onDataChannel([glue](const auto& dc) {
		glue->dc = dc;
		stunseed_glue_setup_dc(glue->dc);
	});
}
}
