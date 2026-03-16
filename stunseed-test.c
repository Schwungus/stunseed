#include <stdlib.h>

#include "raylib.h"
#include "stunseed.h"

#define LOBBY_ID "ABCDEFGH"

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
		if (IsKeyPressed(KEY_Q))
			break;

		if (IsKeyPressed(KEY_K))
			stunseed_echo();

		if (IsKeyPressed(KEY_H))
			stunseed_host(LOBBY_ID, 2);

		if (IsKeyPressed(KEY_J))
			stunseed_join(LOBBY_ID);

		stunseed_update();

		BeginDrawing();
		ClearBackground(RAYWHITE);
		DrawText(TextFormat("we are ID=%s", stunseed_get_our_id()), 5, 5, 30, BLACK);
		EndDrawing();
	}

	CloseWindow();

	return EXIT_SUCCESS;
}
