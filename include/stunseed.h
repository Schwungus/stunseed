#pragma once

#include <stdarg.h>
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
#define STUNSEED_MAX_PEERS (32)

/// The default STUN server to use.
#define STUNSEED_DEFAULT_STUN "stun.l.google.com:19302"

/// The default torrent tracker to leech into for WebRTC signalling.
// #define STUNSEED_DEFAULT_TRACKER "wss://tracker.openwebtorrent.com"
#define STUNSEED_DEFAULT_TRACKER "wss://echo.websocket.org"

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

/// uhh...
void stunseed_echo();

// ----- //
// PEERS //
// ----- //

/// An internal struct for peer-related data.
typedef struct {
	void* glue;
	char* sdp;
} stunseed_peer_info;

/// A unique identifier for a peer within a session, including ourselves.
typedef char stunseed_peer_id[4];

/// Returns our peer's unique identifier.
const char* stunseed_get_our_id();

/// Returns a piece of peer's metadata by the field's name.
stunseed_field stunseed_peer_get(stunseed_peer_id peer, const char* name);

/// Copies a named value into our own metadata dictionary.
void stunseed_peer_set(const char* name, int size, const void* data);

// ---------- //
// CONNECTION //
// ---------- //

/// A secret shared between the peers used to join a stunseed lobby.
typedef char stunseed_lobby_secret[16];

/// Initiate a P2P session for `count` players with a specified secret.
void stunseed_host(const char* secret, int count);

/// Join a P2P session by its secret.
void stunseed_join(const char* secret);

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

#ifdef __cplusplus
}
#endif
