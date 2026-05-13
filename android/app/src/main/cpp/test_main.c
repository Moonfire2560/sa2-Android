#include <SDL.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2", __VA_ARGS__)

int main(int argc, char* argv[]) {
    LOGI("SA2 Android - Starting up!");
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        LOGI("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    
    LOGI("SDL initialized successfully");
    
    // Create window with 16:9 widescreen resolution
    SDL_Window* window = SDL_CreateWindow(
        "Sonic Advance 2",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        426, 240,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN
    );
    
    if (!window) {
        LOGI("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    LOGI("Window created: 426x240 (true 16:9)");
    
    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        LOGI("Renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOGI("Renderer created successfully");
    
    // Main loop - just show a blue screen for now
    SDL_Event event;
    int running = 1;
    
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }
        
        // Clear to blue
        SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        
        SDL_Delay(16); // ~60 FPS
    }
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    LOGI("SA2 Android - Shutting down");
    return 0;
}