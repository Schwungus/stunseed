#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "stunseed.h"

static void tracer(stunseed_log_level level, const char* buf) {
    int rl_level = LOG_INFO;
    if (level != STUNSEED_LOG_INFO)
        rl_level = LOG_WARNING;
    TraceLog(rl_level, "%s", buf);
}

#define FRAMERATE (20)
#define SIZE (20)
#define NETSCALE (8)

enum {
    CHAN_GAME,
    MAX_CHAN,
};

typedef struct {
    int x, y;
    Color color;
} Player;

typedef struct {
    stunseed_webtorrent_id id;
    Player data;
} Peer;

static Peer peers[STUNSEED_MAX_PEERS] = {0};
static Player us = {0};

static void reset() {
    us.x = (GetScreenWidth() - SIZE) / 2;
    us.y = (GetScreenHeight() - SIZE) / 2;
    us.color = RED;

    memset(peers, 0, sizeof(peers));
}

static void draw_player(Player this) {
    DrawRectangle(this.x, this.y, SIZE, SIZE, this.color);
}

static void receive_shit() {
    stunseed_webtorrent_id id;
    static uint8_t buf[2] = {0};
    int size = sizeof(buf);

    while (stunseed_recv(CHAN_GAME, id, buf, &size)) {
        Peer* peer = NULL;

        for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
            if (!memcmp(peers[i].id, id, sizeof(id))) {
                peer = &peers[i];
                goto found;
            }

        for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
            if (!peers[i].id[0]) {
                peer = &peers[i];
                memcpy(peer->id, id, sizeof(id));
                goto found;
            }

    found:
        if (peer) {
            peer->data.color = GREEN;
            peer->data.x = buf[0] * NETSCALE;
            peer->data.y = buf[1] * NETSCALE;
        }
    }
}

static void send_shit() {
    static uint8_t buf[2] = {0};
    buf[0] = us.x / NETSCALE;
    buf[1] = us.y / NETSCALE;

    for (stunseed_peer_info* peer = stunseed_get_peers(); peer; peer = peer->next)
        stunseed_send(CHAN_GAME, peer->id, buf, sizeof(buf));
}

int main(int argc, char* argv[]) {
    (void)argc, (void)argv;

    InitWindow(800, 600, "stunseed");
    stunseed_set_logger(tracer);
    stunseed_set_channel_count(MAX_CHAN);

    SetTargetFPS(FRAMERATE);

    reset();

    while (!WindowShouldClose()) {
        stunseed_update();

        if (IsKeyPressed(KEY_Q))
            break;

        if (IsKeyPressed(KEY_H)) {
            stunseed_host(2);
        } else if (IsKeyPressed(KEY_J)) {
            stunseed_join("damn");
        } else if (IsKeyPressed(KEY_K)) {
            stunseed_disconnect();
            reset();
        }

        const int dx = IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT), dy = IsKeyDown(KEY_DOWN) - IsKeyDown(KEY_UP),
                  vel = 200 / FRAMERATE;
        us.x += dx * vel, us.y += dy * vel;

        send_shit();
        receive_shit();

        BeginDrawing();
        ClearBackground(RAYWHITE);

        draw_player(us);
        for (int i = 0; i < STUNSEED_MAX_PEERS; i++)
            draw_player(peers[i].data);

        int y = 5;
        const int fs = 30;

        DrawText(TextFormat("we are ID=%s", stunseed_get_our_id()), 5, y, fs, BLACK);
        for (stunseed_peer_info* peer = stunseed_get_peers(); peer; peer = peer->next) {
            y += fs;
            DrawText(TextFormat("P=%s", peer->id), 5, y, fs, BLACK);
        }

        EndDrawing();
    }

    stunseed_shutdown();
    CloseWindow();

    return EXIT_SUCCESS;
}
