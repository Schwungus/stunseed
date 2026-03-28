// datachannel-wasm only provides a C++ API. since we're using a C++ linker regardless of the target
// platform (the native libdatachannel is mostly C++ as well, with only an extern "C" compatibility
// layer), i've decided to stuff all of this interoperable C++ glue into its own file.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>

#include <nlohmann/json.hpp>

#include "stunseed.h"

extern "C" void stunseed_glue_init() {
    rtc::Preload();
}

extern "C" void stunseed_glue_cleanup() {
    rtc::Cleanup();
}

static rtc::Configuration stunseed_rtc_config;
static stunseed_webtorrent_id stunseed_lobby_id = {0}, stunseed_peer_id = {0};

static void stunseed_send_json(nlohmann::json);
static void stunseed_announce_all_ready();
static void stunseed_setup_dc(rtc::DataChannel&);

struct stunseed_connection {
    rtc::PeerConnection pc;
    std::shared_ptr<rtc::DataChannel> dc;
    std::string offer_id;
    std::optional<std::string> sdp, remote_id;

    stunseed_connection(const std::string& offer_id) : offer_id(offer_id), pc(stunseed_rtc_config) {
        pc.onLocalDescription([this](const auto& description) { sdp = description; });
        pc.onLocalCandidate([this](const auto&) { sdp = pc.localDescription(); });
    }

    void setup_dc() {
        dc->onOpen([this]() {
            // TODO: use properly.
            dc->send("hi vru!");
        });

        dc->onClosed([this]() {
            // TODO: use properly.
            stunseed_warn("DEAD");
        });

        dc->onMessage([this](auto msg) {
            if (!std::holds_alternative<std::string>(msg))
                return;
            const auto s = std::get<std::string>(msg);
            stunseed_warn("PEER SHEET: %s", s.c_str());
            dc->send(s);
        });
    }
};

static std::unordered_map<std::string, stunseed_connection> stunseed_connections;

static std::optional<rtc::WebSocket> stunseed_tracker_sock;
static std::vector<nlohmann::json> stunseed_ws_queue;

static constexpr const uint64_t stunseed_ns = 1000000000, stunseed_default_announce_interval = 0.5 * stunseed_ns;
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

static void stunseed_send_json(nlohmann::json obj) {
    obj.update({
        {"info_hash", std::string(stunseed_lobby_id, sizeof(stunseed_lobby_id))},
        {"peer_id", std::string(stunseed_peer_id, sizeof(stunseed_peer_id))},
        {"action", "announce"},
    });
    stunseed_ws_queue.push_back(std::move(obj));
}

static void stunseed_announce_all_ready() {
    std::vector<nlohmann::json> offers;

    for (const auto& pair : stunseed_connections) {
        const auto& [id, peer] = pair;
        if (!peer.sdp)
            return;
        offers.push_back({
            {"offer", {{"type", "offer"}, {"sdp", peer.sdp}}},
            {"offer_id", id},
        });
    }

    stunseed_send_json({
        {"downloaded", 0},
        {"left", 1000},
        {"uploaded", 0},
        {"numwant", 1}, // TODO: use STUNSEED_MAX_PEERS
        {"offers", offers},
    });
}

extern "C" void stunseed_update() {
    if (!stunseed_tracker_sock || !stunseed_tracker_sock->isOpen())
        return;

    static uint64_t last_update = 0;
    const uint64_t now = stunseed_time_ns();

    if (!last_update || now - last_update > stunseed_announce_interval)
        stunseed_announce_all_ready(), last_update = now;

    for (const auto& obj : stunseed_ws_queue) {
        stunseed_tracker_sock->send(obj.dump());
        stunseed_warn("SENT: %s", obj.dump().c_str());
    }

    stunseed_ws_queue.clear();
}

extern "C" void stunseed_kill_tracker_sock() {
    if (stunseed_tracker_sock && !stunseed_tracker_sock->isClosed())
        stunseed_tracker_sock->close();
    stunseed_tracker_sock.reset();
}

static void stunseed_on_ws_open() {
    stunseed_info("sock open");
}

static void stunseed_on_ws_closed() {
    // TODO: handle.
    stunseed_info("sock closed");
}

static void stunseed_on_ws_message(const rtc::message_variant& msg) {
    if (!std::holds_alternative<std::string>(msg))
        return;

    const auto& s = std::get<std::string>(msg);
    const auto obj = nlohmann::json::parse(s, nullptr, false);

    if (!obj.is_object())
        return;

    if (obj.contains("interval"))
        // could use obj["interval"] but 120s is way too slow
        stunseed_announce_interval = 5 * stunseed_ns;

    stunseed_warn("WS RECV : %s", obj.dump().c_str());

    if (!obj.contains("offer_id") || !obj.contains("peer_id"))
        return;

    const std::string offer_id = obj["offer_id"];

    if (!stunseed_connections.contains(offer_id))
        stunseed_connections.emplace(offer_id, offer_id);

    auto& peer = stunseed_connections.at(offer_id);
    peer.remote_id = obj["peer_id"];
    peer.pc.onDataChannel([&peer](const auto& dc) {
        peer.dc = dc;
        peer.setup_dc();
        stunseed_warn("GOT DC!!!!!!!");
    });

    std::string type;
    if (obj.contains("offer"))
        type = "offer";
    else if (obj.contains("answer"))
        type = "answer";
    else
        return;

    peer.pc.setRemoteDescription(rtc::Description(obj[type]["sdp"], type));
}

static void stunseed_prepare() {
    stunseed_init();
    stunseed_announce_interval = stunseed_default_announce_interval;
    stunseed_connections.clear();

    stunseed_generate_webtorrent_id(stunseed_peer_id);
    stunseed_info("we are ID=%s", stunseed_peer_id);

    stunseed_kill_tracker_sock();
    stunseed_tracker_sock.emplace();

    stunseed_tracker_sock->onOpen(stunseed_on_ws_open);
    stunseed_tracker_sock->onClosed(stunseed_on_ws_closed);
    stunseed_tracker_sock->onMessage(stunseed_on_ws_message);

    stunseed_tracker_sock->open(STUNSEED_DEFAULT_TRACKER);
}

extern "C" void stunseed_glue_set_stun_server() {
    const std::string scheme = "stun:";
    stunseed_rtc_config.iceServers.emplace_back(scheme + STUNSEED_DEFAULT_STUN);
}

extern "C" void stunseed_glue_set_rtc_logger() {
    rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_create_offers() {
    for (int i = 0; i < 1; i++) { // TODO: use STUNSEED_MAX_PEERS
        std::string offer_id(sizeof(stunseed_webtorrent_id), '\0');
        stunseed_generate_webtorrent_id(offer_id.data());
        stunseed_connections.emplace(offer_id, offer_id);
    }

    for (auto& pair : stunseed_connections) {
        auto& peer = pair.second;
        peer.dc = peer.pc.createDataChannel("bruh");
        peer.setup_dc();
    }
}

#define LOBBY_ID "12345678901234567890"

extern "C" void stunseed_host(int count) {
    // stunseed_generate_webtorrent_id(stunseed_lobby_id);
    memcpy(stunseed_lobby_id, LOBBY_ID, sizeof(stunseed_lobby_id));

    stunseed_prepare();

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

    memcpy(stunseed_lobby_id, LOBBY_ID, sizeof(stunseed_lobby_id));
    stunseed_prepare();

    stunseed_info("joining...");
    stunseed_create_offers();
}
