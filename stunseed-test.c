#include <stdlib.h>

#include "raylib.h"
#include "stunseed.h"

static void tracer(stunseed_log_level level, const char* buf) {
    int rl_level = LOG_INFO;
    if (level != STUNSEED_LOG_INFO)
        rl_level = LOG_WARNING;
    TraceLog(rl_level, "%s", buf);
}

int main(int argc, char* argv[]) {
    (void)argc, (void)argv;

    InitWindow(800, 600, "stunseed");
    stunseed_set_logger(tracer);

    SetTargetFPS(20);

    while (!WindowShouldClose()) {
        stunseed_update();

        if (IsKeyPressed(KEY_Q))
            break;

        if (IsKeyPressed(KEY_H))
            stunseed_host(2);

        if (IsKeyPressed(KEY_J))
            stunseed_join("damn");

        int y = 5;
        const int fs = 30;

        BeginDrawing();
        ClearBackground(RAYWHITE);

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
