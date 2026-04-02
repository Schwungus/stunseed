#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "stunseed-cmake.h" // IWYU pragma: export

// --------- //
// CONSTANTS //
// --------- //

/// Maximum amount of simultaneous peer connections supported.
#define STUNSEED_MAX_PEERS (7)

/// The default STUN server to use.
#define STUNSEED_DEFAULT_STUN "stun.l.google.com:19302"

// ----------- //
// AUXILIARIES //
// ----------- //

/// A metadata field with a size and a pointer to its data.
typedef struct {
    int size;
    const void* data;
} stunseed_field;

/// Initialize the stunseed library. You don't need to call this manually (usually).
void stunseed_init();

/// Call this once before exiting the program to clean up after stunseed.
void stunseed_shutdown();

/// Call this to set the amount of channels stunseed can send and receive on. Defaults to just 1.
///
/// Keep in mind you may not send to a channel you cannot receive on. You are expected to send and receive on the same
/// channel numbers as other peers, and with the same channel count.
void stunseed_set_channel_count(int);

// ----- //
// PEERS //
// ----- //

/// How many bytes to use per WebTorrent ID string.
#define STUNSEED_ID_LENGTH (20)

/// A null-terminated WebTorrent ID string.
typedef char stunseed_webtorrent_id[STUNSEED_ID_LENGTH + 1];

typedef struct stunseed_peer_info {
    struct stunseed_peer_info* next;
    stunseed_webtorrent_id id;
} stunseed_peer_info;

/// Generates a WebTorrent ID string into an output buffer.
void stunseed_generate_webtorrent_id(char*);

/// Returns our peer's unique identifier.
const char* stunseed_get_our_id();

/// Returns a linked list of all peers we're connected to.
stunseed_peer_info* stunseed_get_peers();

/// Returns true if there is a message awaiting on the specified channel.
bool stunseed_poll(int chan);

/// Outputs the next payload in the specified data channel's receiving queue. Returns false if there is no payload and
/// true otherwise.
///
/// Any of the pointers can be set to NULL if you don't need such information about the payload.
bool stunseed_recv(int chan, char* sender, void* data, int bufsize, int* outsize);

/// Sends a payload on the specified data channel to a peer identified by their id string.
///
/// Fails silently; there are no reliability guarantees (yet).
void stunseed_send(int chan, const char* destination, const void* data, int size);

// ---------- //
// CONNECTION //
// ---------- //

/// Returns true if we are currently connected to a WebTorrent tracker to announce ourselves to the swarm.
bool stunseed_is_connected();

/// Initiates a P2P session for `count` players with a random ID.
void stunseed_host(int count);

/// Joins a P2P session by its ID.
void stunseed_join(const char* id);

/// Disconnects you from the lobby.
void stunseed_disconnect();

/// Call this every tick to re-announce yourself to the other peers every now and then.
void stunseed_update();

// --------- //
// CALLBACKS //
// --------- //

/// Sets a function to call every time a peer joins.
///
/// Event data: connected peer's id.
void stunseed_on_peer_join(void (*)(const stunseed_webtorrent_id));

/// Sets a function to call every time a peer leaves.
///
/// Event data: disconnected peer's id.
void stunseed_on_peer_leave(void (*)(const stunseed_webtorrent_id));

// ------- //
// LOGGING //
// ------- //

typedef enum {
    STUNSEED_LOG_INFO,
    STUNSEED_LOG_WARN,
} stunseed_log_level;

/// A logging function type.
typedef void (*stunseed_logger)(stunseed_log_level, const char*);

/// Override the default logger with a custom logging function.
///
/// Pass NULL to reset it back to default.
void stunseed_set_logger(stunseed_logger);

/// The default logging function for stunseed.
void stunseed_default_log(stunseed_log_level, const char*);

/// Like `stunseed_log` but with a varargs list.
void stunseed_log_v(stunseed_log_level, const char*, va_list);

/// Logs a formatted string using stunseed's current logger.
void stunseed_log(stunseed_log_level, const char*, ...);

#define stunseed_info(...) stunseed_log(STUNSEED_LOG_INFO, __VA_ARGS__)
#define stunseed_warn(...) stunseed_log(STUNSEED_LOG_WARN, __VA_ARGS__)

// ----- //
// UTILS //
// ----- //

/// Compute a file's basename (file name without directory).
const char* stunseed_basename(const char* path);

/// Return the current timestamp in nanoseconds.
uint64_t stunseed_time_ns();

#ifdef __cplusplus
}
#endif
