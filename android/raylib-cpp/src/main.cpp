// main.cpp
#include "raylib.h"

int main(void)
{
    InitWindow(0, 0, "RGUI Application");
    SetTargetFPS(60);
    SetExitKey(0);

    while (!WindowShouldClose())
    {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Application OK", 40, 40, 20, DARKGRAY);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
