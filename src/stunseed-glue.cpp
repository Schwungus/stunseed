// datachannel-wasm only provides a C++ API. since we're using a C++ linker regardless of the target
// platform (the native libdatachannel is mostly C++ as well, with only an extern "C" compatibility
// layer), i've decided to stuff all of this interoperable C++ glue into its own file.

#include <memory>
#include <string>
#include <variant>

#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>

#include <nlohmann/json.hpp>

#include "rtc/description.hpp"
#include "stunseed.h"

struct stunseed_glue {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    std::optional<std::string> sdp;
};

static rtc::Configuration stunseed_rtc_config;
static std::map<std::string, stunseed_glue> stunseed_pending_offers, stunseed_pending_answers;
static std::unique_ptr<rtc::WebSocket> stunseed_tracker_sock = nullptr;

static stunseed_webtorrent_id stunseed_lobby_id = {0}, stunseed_peer_id = {0};

static constexpr const uint64_t stunseed_ns = 1000000000, stunseed_default_announce_interval = stunseed_ns / 10;
static uint64_t stunseed_announce_interval = stunseed_default_announce_interval;

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
    stunseed_log_level log_level = stunseed_log_level::STUNSEED_LOG_INFO;

    if (level != rtc::LogLevel::Info)
        log_level = stunseed_log_level::STUNSEED_LOG_WARN;

    stunseed_log(log_level, "%s", line.c_str());
}

extern "C" const char* stunseed_get_our_id() {
    return stunseed_peer_id;
}

static void stunseed_send_json(const nlohmann::json& obj) {
    if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
        stunseed_tracker_sock->send(obj.dump()), stunseed_warn("SENT: %s", obj.dump().c_str());
}

static void stunseed_maybe_announce() {
    std::vector<nlohmann::json> offers;

    for (const auto& pair : stunseed_pending_offers) {
        const auto [id, peer] = pair;
        if (!peer.sdp)
            return;
        offers.push_back({
            {"offer", {{"type", "offer"}, {"sdp", peer.sdp}}},
            {"offer_id", id},
        });
    }

    std::string info_hash(stunseed_lobby_id, sizeof(stunseed_lobby_id));

    nlohmann::json params = {
        {"action", "announce"},
        {"info_hash", std::move(info_hash)},
        {"peer_id", stunseed_peer_id},
        {"downloaded", 0},
        {"left", 1000},
        {"uploaded", 0},
        {"numwant", STUNSEED_MAX_PEERS},
        {"offers", offers},
    };

    stunseed_send_json(params);
}

extern "C" void stunseed_update() {
    static uint64_t last_update = 0;
    const uint64_t now = stunseed_time_ns();

    if (!last_update || now - last_update > stunseed_announce_interval)
        stunseed_maybe_announce(), last_update = now;
}

extern "C" void stunseed_kill_tracker_sock() {
    if (stunseed_tracker_sock && stunseed_tracker_sock->isOpen())
        stunseed_tracker_sock->close();
}

static void stunseed_setup_dc(stunseed_glue& peer) {
    peer.dc->onOpen([&peer]() {
        // TODO: use properly.
        peer.dc->send("hi vru!");
    });

    peer.dc->onClosed([]() {
        // TODO: use properly.
    });

    peer.dc->onMessage([&peer](const auto& msg) {
        if (!std::holds_alternative<std::string>(msg))
            return;
        const auto s = std::get<std::string>(msg);
        stunseed_warn("PEER SHEET: %s", s.c_str());
        peer.dc->send(s);
    });
}

static void stunseed_on_ws_closed() {
    // TODO: handle.
}

static void stunseed_on_ws_message(const rtc::message_variant& msg) {
    if (!std::holds_alternative<std::string>(msg))
        return;

    const auto& s = std::get<std::string>(msg);
    stunseed_warn("recv: %s", s.c_str());

    const auto obj = nlohmann::json::parse(s, nullptr, false);
    if (!obj.is_object())
        return;

    if (obj.contains("interval"))
        stunseed_announce_interval = (int)obj["interval"] * stunseed_ns;

    if (obj.contains("answer") && obj.contains("offer_id")) {
        std::string offer_id = obj["offer_id"];

        if (!stunseed_pending_offers.contains(offer_id))
            return;

        auto& peer = stunseed_pending_offers[offer_id];
        rtc::Description desc(obj["answer"]["sdp"], "answer");
        peer.pc->setRemoteDescription(desc);
    } else if (obj.contains("offer") && obj.contains("offer_id")) {
        std::string their_offer = obj["offer_id"], their_id = obj["peer_id"];
        stunseed_pending_answers.emplace(their_offer, stunseed_glue());
        auto& peer = stunseed_pending_answers[their_offer];

        peer.pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);

        peer.pc->onLocalDescription([their_offer, their_id](const auto& description) {
            std::string info_hash(stunseed_lobby_id, sizeof(stunseed_lobby_id));
            std::string our_id(stunseed_peer_id, sizeof(stunseed_peer_id));

            nlohmann::json params = {
                {"action", "announce"},
                {"info_hash", std::move(info_hash)},
                {"peer_id", std::move(our_id)},
                {"to_peer_id", their_id},
                {"offer_id", their_offer},
                {"answer", {{"type", "answer"}, {"sdp", description}}},
            };

            stunseed_send_json(params);
        });

        peer.pc->onDataChannel([&peer](auto dc) {
            stunseed_warn("BRO COOL!!!");
            peer.dc = std::move(dc);
            stunseed_setup_dc(peer);
        });

        rtc::Description desc(obj["offer"]["sdp"], (std::string)obj["offer"]["type"]);
        peer.pc->setRemoteDescription(desc);
    }
}

static void stunseed_prepare() {
    stunseed_init();
    stunseed_announce_interval = stunseed_default_announce_interval;

    stunseed_kill_tracker_sock();
    stunseed_tracker_sock = std::make_unique<rtc::WebSocket>();

    stunseed_tracker_sock->onClosed(stunseed_on_ws_closed);
    stunseed_tracker_sock->onMessage(stunseed_on_ws_message);
    stunseed_tracker_sock->open(STUNSEED_DEFAULT_TRACKER);

    stunseed_pending_offers.clear();
    stunseed_pending_answers.clear();

    stunseed_generate_webtorrent_id(stunseed_peer_id);
    stunseed_info("we are ID=%s", stunseed_peer_id);
}

extern "C" void stunseed_glue_set_stun_server() {
    const std::string scheme = "stun:";
    stunseed_rtc_config.iceServers.emplace_back(scheme + STUNSEED_DEFAULT_STUN);
}

extern "C" void stunseed_glue_set_rtc_logger() {
    rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_create_offers() {
    for (int i = 0; i < 3; i++) { // TODO: use a sensible offer count
        std::string offer_id(sizeof(stunseed_webtorrent_id), '\0');
        stunseed_generate_webtorrent_id(offer_id.data());

        stunseed_pending_offers.emplace(offer_id, stunseed_glue());
        auto& peer = stunseed_pending_offers[offer_id];

        peer.pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);

        peer.pc->onLocalDescription([&peer](const rtc::Description& description) {
            peer.sdp = description;
            stunseed_maybe_announce();
        });

        /*glue->pc->onLocalCandidate(
                [](const rtc::Candidate& candidate) { stunseed_announce(std::string(candidate).c_str()); });*/

        peer.pc->onStateChange([](rtc::PeerConnection::State state) {
            if (state >= rtc::PeerConnection::State::Disconnected)
                (void)0; // TODO: handle disconnection
        });

        peer.dc = peer.pc->createDataChannel("bruh");
        stunseed_setup_dc(peer);
    }
}

#define LOBBY_ID "12345678901234567890"

extern "C" void stunseed_host(int count) {
    stunseed_prepare();
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

extern "C" void stunseed_join(const char* id) {
    (void)id;

    stunseed_prepare();
    memcpy(stunseed_lobby_id, LOBBY_ID, sizeof(stunseed_lobby_id));

    stunseed_info("joining...");
    stunseed_create_offers();
}
