// datachannel-wasm only provides a C++ API. due to this limitation, we're using a C++ compiler/linker regardless of the
// target platform. the native libdatachannel is backed by C++ as well. so i've decided to stuff all of this
// interoperable C++ glue into its own file.

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

using namespace std::string_literals;

static const std::vector<std::string> stunseed_webtorrent_trackers{
    "wss://tracker.openwebtorrent.com",
    "wss://tracker.webtorrent.dev",
    "wss://tracker.btorrent.xyz",
};

static constexpr const uint64_t stunseed_announce_interval = 3000000000;
static const rtc::Configuration stunseed_rtc_config{
    .iceServers = {"stun:"s + STUNSEED_DEFAULT_STUN},
};

#define STUNSEED_PAYLOAD_HEADER_SIZE (1)

struct stunseed_packet {
    std::string peer;
    std::vector<std::byte> payload;

    stunseed_packet(const std::string& peer, const std::vector<std::byte>& data) : peer(peer), payload(data) {}
};

static void (*stunseed_peer_join_cb)(const stunseed_webtorrent_id) = nullptr;
static void (*stunseed_peer_leave_cb)(const stunseed_webtorrent_id) = nullptr;

extern "C" void stunseed_on_peer_join(void (*cb)(const stunseed_webtorrent_id)) {
    stunseed_peer_join_cb = cb;
}

extern "C" void stunseed_on_peer_leave(void (*cb)(const stunseed_webtorrent_id)) {
    stunseed_peer_leave_cb = cb;
}

struct stunseed_sock {
    std::unique_ptr<rtc::WebSocket> ws;
    std::vector<nlohmann::json> out_queue;

    stunseed_sock(rtc::WebSocket* ws) : ws(ws) {}

    ~stunseed_sock() {
        if (ws->isOpen())
            ws->close();
    }
};

struct stunseed_connection;

static struct stunseed_glue_t {
    // NOTE: indexed by offer id.
    std::unordered_map<std::string, stunseed_connection> connections;
    std::vector<std::vector<stunseed_packet>> recv;

    std::unordered_map<std::string, stunseed_sock> socks;
    std::optional<std::string> lobby_id, peer_id;

    stunseed_glue_t() {
        reset();
    }

    friend void stunseed_disconnect();

  private:
    void reset() {
        lobby_id = peer_id = std::nullopt;

        size_t old_recv_size = recv.size();
        connections.clear(), recv.clear();
        recv.resize(old_recv_size);

        socks.clear();
        for (const auto& tracker : stunseed_webtorrent_trackers)
            socks.emplace(tracker, new rtc::WebSocket);
    }
} stunseed_glue;

static void stunseed_announce();

struct stunseed_connection {
    rtc::PeerConnection pc;
    std::shared_ptr<rtc::DataChannel> dc;
    std::optional<std::string> remote_id;
    const std::string offer_id;

    stunseed_connection(const std::string& offer_id) : offer_id(offer_id), pc(stunseed_rtc_config), dc(nullptr) {}

    void setup_dc() {
        dc->onOpen([this]() {
            if (stunseed_peer_join_cb)
                stunseed_peer_join_cb(remote_id->c_str());
        });

        dc->onClosed([this]() {
            if (stunseed_peer_leave_cb)
                stunseed_peer_leave_cb(remote_id->c_str());
            stunseed_glue.connections.erase(offer_id);
        });

        dc->onMessage([this](auto msg) {
            if (!remote_id.has_value()) // shouldn't be possible but ok
                return;
            if (!std::holds_alternative<std::vector<std::byte>>(msg))
                return;

            auto payload = std::get<std::vector<std::byte>>(msg);
            if (payload.size() < STUNSEED_PAYLOAD_HEADER_SIZE)
                return;

            const auto chan = (uint8_t)payload[0];
            if (chan >= stunseed_glue.recv.size())
                return;

            std::vector<std::byte> sub(payload.size() - STUNSEED_PAYLOAD_HEADER_SIZE); // TODO: unkludge
            memcpy(&sub[0], &payload[STUNSEED_PAYLOAD_HEADER_SIZE], sub.size());

            auto& queue = stunseed_glue.recv[chan];
            queue.emplace_back(*remote_id, std::move(sub));
        });
    }
};

extern "C" void stunseed_disconnect() {
    stunseed_glue.reset();
}

extern "C" void stunseed_glue_init() {
    rtc::Preload();
}

extern "C" void stunseed_glue_cleanup() {
    rtc::Cleanup();
}

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
    stunseed_log_level log_level = stunseed_log_level::STUNSEED_LOG_INFO;

    if (level != rtc::LogLevel::Info)
        log_level = stunseed_log_level::STUNSEED_LOG_WARN;

    stunseed_log(log_level, "%s", line.c_str());
}

extern "C" bool stunseed_is_connected() {
    for (const auto& pair : stunseed_glue.socks)
        if (pair.second.ws->isOpen())
            return true;
    return false;
}

extern "C" const char* stunseed_get_our_id() {
    return stunseed_is_connected() ? stunseed_glue.peer_id->c_str() : nullptr;
}

extern "C" void stunseed_set_channel_count(int count) {
    stunseed_glue.recv.resize(count);
}

extern "C" stunseed_peer_info* stunseed_get_peers() {
    static stunseed_peer_info mem[STUNSEED_MAX_PEERS] = {0};

    stunseed_peer_info *cur = mem, *root = nullptr;
    size_t count = 0;

    for (const auto& pair : stunseed_glue.connections) {
        const auto& peer = pair.second;

        if (!peer.remote_id.has_value() || peer.dc == nullptr)
            continue;

        if (cur > mem)
            (cur - 1)->next = cur;

        if (count >= STUNSEED_MAX_PEERS)
            break;

        memset(cur, 0, sizeof(*cur));
        memcpy(cur->id, peer.remote_id->data(), STUNSEED_ID_LENGTH);

        if (!root)
            root = cur;
        cur += 1;
    }

    return root;
}

extern "C" bool stunseed_recv(int chan, char* sender, void* data, int bufsize, int* outsize) {
    if (chan < 0 || chan >= stunseed_glue.recv.size())
        return false;

    if (stunseed_glue.recv[chan].empty())
        return false;

    auto& queue = stunseed_glue.recv[chan];
    const stunseed_packet packet = queue.front();
    queue.erase(queue.begin());

    if (packet.payload.size() > bufsize)
        return false;

    if (sender)
        memcpy(sender, packet.peer.data(), sizeof(stunseed_webtorrent_id));
    if (data)
        memcpy(data, packet.payload.data(), packet.payload.size());
    if (outsize)
        *outsize = (int)packet.payload.size();

    return true;
}

extern "C" void stunseed_send(int chan, const char* destination, const void* data, int size) {
    if (chan < 0 || chan >= stunseed_glue.recv.size())
        return;

    stunseed_connection* conn = nullptr;

    for (auto& pair : stunseed_glue.connections) {
        auto& c = pair.second;
        if (!c.remote_id.has_value() || c.dc == nullptr || !c.dc->isOpen())
            continue;
        if (!memcmp(c.remote_id->data(), destination, STUNSEED_ID_LENGTH)) {
            conn = &c;
            break;
        }
    }

    if (!conn)
        return;

    std::vector<uint8_t> buf(STUNSEED_PAYLOAD_HEADER_SIZE + size); // TODO: unkludge
    buf[0] = chan;
    memcpy(&buf[STUNSEED_PAYLOAD_HEADER_SIZE], data, size);
    conn->dc->send((const rtc::byte*)buf.data(), buf.size());
}

static void stunseed_send_json(const std::string& tracker, nlohmann::json obj) {
    obj.update({
        {"info_hash", stunseed_glue.lobby_id},
        {"peer_id", stunseed_glue.peer_id},
        {"action", "announce"},
    });

    stunseed_glue.socks.at(tracker).out_queue.push_back(std::move(obj));
}

static void stunseed_announce() {
    std::vector<nlohmann::json> offers;

    for (const auto& pair : stunseed_glue.connections) {
        const auto& [id, peer] = pair;
        const std::optional<std::string>& sdp = peer.pc.localDescription();
        if (!sdp.has_value())
            continue;
        offers.push_back({
            {"offer", {{"type", "offer"}, {"sdp", sdp}}},
            {"offer_id", id},
        });
    }

    for (const auto& pair : stunseed_glue.socks) {
        nlohmann::json obj{
            {"downloaded", 0},
            {"left", 1000},
            {"uploaded", 0},
            {"numwant", STUNSEED_MAX_PEERS},
            {"offers", offers},
        };

        stunseed_send_json(pair.first, std::move(obj));
    }
}

extern "C" void stunseed_update() {
    if (!stunseed_is_connected())
        return;

    static uint64_t last_update = 0;
    const uint64_t now = stunseed_time_ns();

    if (!last_update || now - last_update > stunseed_announce_interval)
        stunseed_announce(), last_update = now;

    for (auto& pair : stunseed_glue.socks) {
        auto& sock = pair.second;

        if (!sock.ws->isOpen())
            continue;

        for (const auto& obj : sock.out_queue)
            sock.ws->send(obj.dump());

        sock.out_queue.clear();
    }
}

static void stunseed_on_ws_closed() {
    if (!stunseed_is_connected())
        stunseed_disconnect();
}

static void stunseed_on_ws_message(const rtc::message_variant& msg) {
    if (!std::holds_alternative<std::string>(msg))
        return;

    const auto& s = std::get<std::string>(msg);
    const auto obj = nlohmann::json::parse(s, nullptr, false);

    // stunseed_warn("WS RECV : %s", obj.dump().c_str());
    if (!obj.is_object())
        return;
    if (!obj.contains("offer_id") || !obj.contains("peer_id"))
        return;
    if (std::string(obj["peer_id"]).length() != STUNSEED_ID_LENGTH)
        return;

    std::string offer_id = obj["offer_id"];
    offer_id.resize(STUNSEED_ID_LENGTH);

    if (!stunseed_glue.connections.contains(offer_id)) {
        // HACK: erase duplicate peers with different offers until i figure out how to handle such cases properly.
        std::erase_if(stunseed_glue.connections, [&obj](const auto& pair) {
            return pair.second.remote_id == (std::string)obj["peer_id"];
        });

        stunseed_glue.connections.emplace(offer_id, offer_id);
    }

    auto& peer = stunseed_glue.connections.at(offer_id);
    peer.remote_id = obj["peer_id"];
    peer.remote_id->resize(STUNSEED_ID_LENGTH);

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
    stunseed_disconnect();

    stunseed_glue.peer_id = std::string(STUNSEED_ID_LENGTH, '0');
    stunseed_generate_webtorrent_id(stunseed_glue.peer_id->data());
    stunseed_info("we are ID=%s", stunseed_glue.peer_id->c_str());

    for (auto& [tracker, sock] : stunseed_glue.socks) {
        sock.ws->onClosed(stunseed_on_ws_closed);
        sock.ws->onMessage(stunseed_on_ws_message);
        sock.ws->open(tracker);
    }
}

extern "C" void stunseed_glue_set_rtc_logger() {
    rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_create_offers() {
    while (stunseed_glue.connections.size() < STUNSEED_MAX_PEERS) {
        std::string offer_id(STUNSEED_ID_LENGTH, 0);
        stunseed_generate_webtorrent_id(offer_id.data());
        stunseed_glue.connections.emplace(offer_id, offer_id);
    }

    for (auto& pair : stunseed_glue.connections) {
        auto& peer = pair.second;
        peer.dc = peer.pc.createDataChannel("bruh");
        peer.setup_dc();
    }
}

#define LOBBY_ID "12345678901234567890"

extern "C" void stunseed_host(int count) {
    stunseed_prepare();
    stunseed_glue.lobby_id = LOBBY_ID;

    if (count > STUNSEED_MAX_PEERS) {
        count = STUNSEED_MAX_PEERS;
        stunseed_warn("requested %d peers > %d max", count, STUNSEED_MAX_PEERS);
    }

    if (count < 1) {
        count = 1;
        stunseed_warn("requested <1 peers", count);
    }

    stunseed_create_offers();
    stunseed_info("hosting. %d peers max", count);
}

extern "C" void stunseed_join(const char* id) {
    (void)id;

    stunseed_prepare();
    stunseed_glue.lobby_id = LOBBY_ID;

    stunseed_create_offers();
    stunseed_info("joining...");
}
