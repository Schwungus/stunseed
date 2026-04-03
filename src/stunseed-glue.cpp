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

    // FIXME: multiple instances of the same peer can connect if they're from a different tracker/swarm.

    // "wss://tracker.webtorrent.dev",
    // "wss://tracker.btorrent.xyz",
};

static constexpr const uint64_t stunseed_announce_interval = 5000000000, stunseed_timeout_threshold = 5000000000,
                                stunseed_ping_interval = 2000000000;
static const rtc::Configuration stunseed_rtc_config{
    .iceServers = {"stun:"s + STUNSEED_DEFAULT_STUN},
};

#define STUNSEED_PAYLOAD_HEADER_SIZE (1)

struct stunseed_payload {
    std::string peer; // could be the sender or, in the future, the recipient.
    std::vector<std::byte> contents;

    stunseed_payload(const std::string& peer, const std::vector<std::byte>& contents)
        : peer(peer), contents(contents) {}
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
    std::shared_ptr<rtc::WebSocket> ws;
    std::vector<nlohmann::json> out_queue;
    bool dead = false;

    stunseed_sock(const std::string& tracker) : ws(new rtc::WebSocket) {
        ws->onClosed([this, tracker]() {
            stunseed_warn("lost tracker: %s", tracker.c_str());
            dead = true;
        });
    }

    bool is_open() const {
        return !dead && ws->isOpen();
    }

    ~stunseed_sock() {
        if (ws->isOpen())
            ws->close();
    }
};

struct stunseed_channel;

static struct stunseed_glue_t {
    std::unordered_map<std::string, std::shared_ptr<stunseed_channel>> peers; // indexed by PEER ID

    std::unordered_map<std::string, std::shared_ptr<stunseed_channel>> offers; // indexed by OFFER ID

    std::unordered_map<std::string, stunseed_sock> socks;
    std::vector<std::vector<stunseed_payload>> recv;
    std::optional<std::string> lobby_id = std::nullopt, peer_id = std::nullopt;
    uint64_t last_announce = 0;

    stunseed_glue_t() {
        reset();
    }

    void reset() {
        peers.clear(), recv.clear(), lobby_id.reset(), peer_id.reset();
        last_announce = 0;

        socks.clear();
        for (const auto& tracker : stunseed_webtorrent_trackers)
            socks.insert_or_assign(tracker, stunseed_sock(tracker));
    }
} stunseed_glue;

struct stunseed_channel {
    std::shared_ptr<rtc::PeerConnection> pc = nullptr;
    std::shared_ptr<rtc::DataChannel> dc = nullptr;

    uint64_t last_received_ping = 0, last_sent_ping = 0;
    bool dead = false;

    bool is_open() const {
        return dc != nullptr && dc->isOpen();
    }
};

static void stunseed_send_json(const std::string&, nlohmann::json);

static void stunseed_setup_trickle_pc(
    const std::string& tracker, const std::shared_ptr<stunseed_channel>& peer, const std::string& remote_peer_id) {
    peer->pc->onLocalCandidate([tracker, remote_peer_id](const auto& cand) {
        const nlohmann::json obj = {
            {"to_peer_id", remote_peer_id},
            {"candidate", {{"candidate", std::string(cand)}, {"sdpMid", cand.mid()}}},
        };

        stunseed_send_json(tracker, obj);
    });
}

static void stunseed_setup_dc(const std::shared_ptr<stunseed_channel>& peer, const std::string& peer_id) {
    std::weak_ptr<stunseed_channel> peerw = peer;

    const auto do_open = [peer_id]() {
        if (stunseed_peer_join_cb)
            stunseed_peer_join_cb(peer_id.c_str());
    };

    if (peer->dc->isOpen())
        do_open();
    else
        peer->dc->onOpen(do_open);

    peer->dc->onClosed([peerw, peer_id]() {
        if (!peerw.expired())
            peerw.lock()->dead = true;
    });

    peer->dc->onMessage([peerw, peer_id](const auto& msg) {
        if (peerw.expired())
            return;

        auto peer = peerw.lock();

        if (std::holds_alternative<std::string>(msg)) {
            const auto payload = std::get<std::string>(msg);
            if (payload == "ping")
                peer->last_received_ping = stunseed_time_ns();
        } else {
            const auto payload = std::get<std::vector<std::byte>>(msg);
            if (payload.size() < STUNSEED_PAYLOAD_HEADER_SIZE)
                return;

            const auto chan = (uint8_t)payload[0];
            if (chan >= stunseed_glue.recv.size())
                return;

            std::vector<std::byte> sub(payload.size() - STUNSEED_PAYLOAD_HEADER_SIZE); // TODO: unkludge
            memcpy(&sub[0], &payload[STUNSEED_PAYLOAD_HEADER_SIZE], sub.size());

            auto& queue = stunseed_glue.recv[chan];
            queue.emplace_back(peer_id, std::move(sub));
        }
    });
}

extern "C" void stunseed_disconnect() {
    const size_t old_recv_size = stunseed_glue.recv.size();
    stunseed_glue.reset();
    stunseed_glue.recv.resize(old_recv_size);
}

extern "C" void stunseed_glue_init() {
    rtc::Preload();
}

extern "C" void stunseed_glue_cleanup() {
    rtc::Cleanup();
}

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
    auto log_level = stunseed_log_level::STUNSEED_LOG_INFO;

    if (level != rtc::LogLevel::Info)
        log_level = stunseed_log_level::STUNSEED_LOG_WARN;

    stunseed_log(log_level, "%s", line.c_str());
}

extern "C" bool stunseed_is_connected() {
    for (const auto& [_, sock] : stunseed_glue.socks)
        if (sock.is_open())
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
    if (!stunseed_is_connected())
        return nullptr;

    static stunseed_peer_info mem[STUNSEED_MAX_PEERS] = {0};
    stunseed_peer_info *cur = mem, *root = nullptr;

    for (const auto& [id, peer] : stunseed_glue.peers) {
        if (!peer->is_open())
            continue;

        if (cur > mem)
            (cur - 1)->next = cur;

        if (cur >= mem + STUNSEED_MAX_PEERS) // TODO: verify if this should be >= or >
            break;

        memset(cur, 0, sizeof(*cur));
        memcpy(cur->id, id.data(), STUNSEED_ID_LENGTH);

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
    const auto payload = queue.front();
    queue.erase(queue.begin());

    if (payload.contents.size() > bufsize) {
        stunseed_warn("skipping size=%d payload: buffer size=%d insufficient", payload.contents.size(), bufsize);
        return false;
    }

    if (sender)
        memcpy(sender, payload.peer.data(), sizeof(stunseed_webtorrent_id));
    if (data)
        memcpy(data, payload.contents.data(), payload.contents.size());
    if (outsize)
        *outsize = (int)payload.contents.size();

    return true;
}

extern "C" void stunseed_send(int chan, const char* destination, const void* data, int size) {
    if (chan < 0 || chan >= stunseed_glue.recv.size())
        return;

    if (!stunseed_glue.peers.contains(destination))
        return;

    auto& peer = stunseed_glue.peers.at(destination);

    if (!peer->is_open())
        return;

    std::vector<uint8_t> buf(STUNSEED_PAYLOAD_HEADER_SIZE + size); // TODO: unkludge
    buf[0] = chan;
    memcpy(&buf[STUNSEED_PAYLOAD_HEADER_SIZE], data, size);
    peer->dc->send((const rtc::byte*)buf.data(), buf.size());
}

static void stunseed_send_json(const std::string& tracker, nlohmann::json obj) {
    obj.update({
        {"action", "announce"},
        {"info_hash", stunseed_glue.lobby_id},
        {"peer_id", stunseed_glue.peer_id},
        {"downloaded", 0},
        {"left", 1000},
        {"uploaded", 0},
    });

    auto& sock = stunseed_glue.socks.at(tracker);
    sock.out_queue.push_back(obj);
}

static void stunseed_announce() {
    std::vector<nlohmann::json> offers;

    for (const auto& [id, offer] : stunseed_glue.offers) {
        const std::optional<std::string>& sdp = offer->pc->localDescription();

        if (!sdp.has_value())
            continue;

        offers.push_back({
            {"offer", {{"type", "offer"}, {"sdp", sdp}}},
            {"offer_id", id},
        });
    }

    const nlohmann::json obj{
        {"offers", offers},
        {"numwant", STUNSEED_MAX_PEERS},
    };

    for (auto& [tracker, _] : stunseed_glue.socks)
        stunseed_send_json(tracker, obj);
}

extern "C" void stunseed_update() {
    const uint64_t now = stunseed_time_ns();

    for (auto& [_, peer] : stunseed_glue.peers) {
        if (!peer->last_received_ping) {
            peer->last_received_ping = now;
        } else if (now - peer->last_received_ping >= stunseed_timeout_threshold) {
            peer->dead = true;
            continue;
        }

        if (peer->is_open() && (!peer->last_sent_ping || now - peer->last_sent_ping >= stunseed_ping_interval))
            peer->dc->send("ping"), peer->last_sent_ping = now;
    }

    std::erase_if(stunseed_glue.peers, [](const auto& pair) {
        const auto& [id, peer] = pair;
        const auto dead = peer->dead;
        if (dead && stunseed_peer_leave_cb)
            stunseed_peer_leave_cb(id.c_str());
        return dead;
    });

    if (!stunseed_is_connected())
        return;

    if (!stunseed_glue.last_announce || now - stunseed_glue.last_announce > stunseed_announce_interval)
        stunseed_announce(), stunseed_glue.last_announce = now;

    for (auto& [_, sock] : stunseed_glue.socks) {
        if (!sock.is_open())
            continue;

        for (const auto& obj : sock.out_queue)
            sock.ws->send(obj.dump());

        sock.out_queue.clear();
    }
}

static void stunseed_on_ws_message(const std::string& tracker, const rtc::message_variant& msg) {
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

    std::string remote_peer_id = obj["peer_id"];
    remote_peer_id.resize(STUNSEED_ID_LENGTH);

    if (obj.contains("offer")) {
        if (stunseed_glue.peers.contains(remote_peer_id))
            return;

        auto peer = std::make_shared<stunseed_channel>();
        std::weak_ptr<stunseed_channel> peerw = peer;

        peer->pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);
        stunseed_setup_trickle_pc(tracker, peer, remote_peer_id);

        peer->pc->onDataChannel([peerw, remote_peer_id](const auto& dc) {
            if (!peerw.expired()) {
                auto peer = peerw.lock();
                peer->dc = dc;
                stunseed_setup_dc(peer, remote_peer_id);
            }
        });

        peer->pc->onLocalDescription([tracker, offer_id, remote_peer_id](const auto& desc) {
            const std::string sdp = desc;
            const nlohmann::json obj{
                {"offer_id", offer_id},
                {"to_peer_id", remote_peer_id},
                {"answer", {{"type", "answer"}, {"sdp", sdp}}},
            };
            stunseed_send_json(tracker, obj);
        });

        stunseed_glue.peers.emplace(remote_peer_id, peer);
        peer->pc->setRemoteDescription(rtc::Description(obj["offer"]["sdp"], "offer"));
    } else if (obj.contains("answer")) {
        if (!stunseed_glue.offers.contains(offer_id))
            return;

        auto offer = stunseed_glue.offers.extract(offer_id).mapped();
        stunseed_setup_dc(offer, remote_peer_id);

        auto [it, _] = stunseed_glue.peers.emplace(remote_peer_id, offer);
        stunseed_setup_trickle_pc(tracker, it->second, it->first);
        it->second->pc->setRemoteDescription(rtc::Description(obj["answer"]["sdp"], "answer"));
    } else if (obj.contains("candidate")) {
        if (!stunseed_glue.peers.contains(remote_peer_id))
            return;

        auto& peer = stunseed_glue.peers.at(remote_peer_id);
        const auto& c = obj["candidate"];
        peer->pc->addRemoteCandidate(rtc::Candidate(c["candidate"], c["sdpMid"]));
    }
}

static void stunseed_prepare() {
    stunseed_init();
    stunseed_disconnect();

    stunseed_glue.peer_id = std::string(STUNSEED_ID_LENGTH, '0');
    stunseed_generate_webtorrent_id(stunseed_glue.peer_id->data());
    stunseed_info("we are ID=%s", stunseed_glue.peer_id->c_str());

    for (auto& [tracker, sock] : stunseed_glue.socks) {
        sock.ws->onMessage([tracker](const auto& msg) {
            stunseed_on_ws_message(tracker, msg);
        });

        sock.ws->open(tracker);
    }
}

extern "C" void stunseed_glue_set_rtc_logger() {
    rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}

static void stunseed_create_offers() {
    while (stunseed_glue.offers.size() < STUNSEED_MAX_PEERS) {
        std::string offer_id(STUNSEED_ID_LENGTH, 0);
        stunseed_generate_webtorrent_id(offer_id.data());

        auto offer = std::make_shared<stunseed_channel>();
        offer->pc = std::make_shared<rtc::PeerConnection>(stunseed_rtc_config);
        offer->dc = offer->pc->createDataChannel("bruh");
        stunseed_glue.offers.emplace(offer_id, offer);
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
