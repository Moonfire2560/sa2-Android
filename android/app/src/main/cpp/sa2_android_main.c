#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspgu.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SA2", __VA_ARGS__)
#endif

#include <SDL.h>

#include "global.h"
#include "core.h"
#include "lib/agb_flash/flash_internal.h"
#include "platform/shared/dma.h"
#include "platform/shared/input.h"
#include "platform/shared/video/gpsp_renderer.h"

#if ENABLE_AUDIO
#include "platform/shared/audio/cgb_audio.h"
#endif

ALIGNED(256) uint16_t gameImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];

#if ENABLE_VRAM_VIEW
uint16_t vramBuffer[VRAM_VIEW_WIDTH * VRAM_VIEW_HEIGHT];
#endif

SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
SDL_Window *vramWindow;
SDL_Renderer *vramRenderer;
SDL_Texture *vramTexture;
#endif
#define INITIAL_VIDEO_SCALE 1
unsigned int videoScale = INITIAL_VIDEO_SCALE;
unsigned int preFullscreenVideoScale = INITIAL_VIDEO_SCALE;

bool speedUp = false;
bool videoScaleChanged = false;
bool isRunning = true;
bool paused = false;
bool stepOneFrame = false;
bool headless = false;

#ifdef __PSP__
static SDL_Joystick *joystick = NULL;
static SDL_Rect pspDestRect;
#endif

#ifdef __ANDROID__
// Virtual touch controls state
static bool virtualButtonsPressed[10] = {false}; // A, B, Start, Select, L, R, Up, Down, Left, Right
static int touchDPadCenterX = 120;
static int touchDPadCenterY = 600; // Will be adjusted based on screen height
static int touchButtonA_X = 1800; // Will be adjusted based on screen width
static int touchButtonA_Y = 600;
static int touchButtonB_X = 1650;
static int touchButtonB_Y = 450;
#endif

double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
double accumulator = 0.0;

static FILE *sSaveFile = NULL;

extern void AgbMain(void);
void DoSoftReset(void) {};

void ProcessSDLEvents(void);
void VDraw(SDL_Texture *texture);
void VramDraw(SDL_Texture *texture);

static void ReadSaveFile(char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

u16 Platform_GetKeyInput(void);

#ifdef _WIN32
void *Platform_malloc(size_t numBytes) { return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, numBytes); }
void Platform_free(void *ptr) { HeapFree(GetProcessHeap(), 0, ptr); }
#endif

#ifdef __PSP__
PSP_MODULE_INFO("SonicAdvance2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

unsigned int sce_newlib_stack_size = 512 * 1024;

extern bool isRunning;

int exitCallback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    isRunning = false;
    return 0;
}

int callbackThread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setupPspCallbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
    return thid;
}
#endif

int main(int argc, char **argv)
{
#ifdef __PSP__
    setupPspCallbacks();
#endif

#ifdef __ANDROID__
    LOGI("SA2 Android - Initializing");
    
    // Get Android internal storage path
    const char* internalPath = SDL_AndroidGetInternalStoragePath();
    char savePath[512];
    snprintf(savePath, sizeof(savePath), "%s/sa2.sav", internalPath);
    LOGI("Save path: %s", savePath);
#else
    const char *savePath = "sa2.sav";
#endif

    const char *headlessEnv = getenv("HEADLESS");

    if (headlessEnv && strcmp(headlessEnv, "true") == 0) {
        headless = true;
    }

    const char *parentEnv = getenv("SIO_PARENT");

    if (parentEnv && strcmp(parentEnv, "true") == 0) {
        SIO_MULTI_CNT->id = 0;
        SIO_MULTI_CNT->si = 1;
        SIO_MULTI_CNT->sd = 1;
        SIO_MULTI_CNT->enable = false;
    }

    // Open an output console on Windows
#if (defined _WIN32) && (DEBUG != 0)
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    freopen("CON", "w", stdout);
#endif

#ifdef __ANDROID__
    ReadSaveFile(savePath);
#else
    ReadSaveFile("sa2.sav");
#endif

    // Prevent the multiplayer screen from being drawn ( see core.c:EngineInit() )
    REG_RCNT = 0x8000;
    REG_KEYINPUT = 0x3FF;

    if (headless) {
#if ENABLE_AUDIO
        // Required or it makes an infinite loop
        cgb_audio_init(48000);
#endif
        AgbMain();
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
#ifdef __ANDROID__
        LOGE("SDL Init failed: %s", SDL_GetError());
#endif
        return 1;
    }

#ifdef __ANDROID__
    LOGI("SDL initialized successfully");
    
    // Set Android-specific hints
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1");
#endif

#ifdef __PSP__
    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
    }
#endif

#ifdef TITLE_BAR
    const char *title = STR(TITLE_BAR);
#else
    const char *title = "Sonic Advance 2";
#endif

#ifdef __ANDROID__
    // On Android, create fullscreen window
    sdlWindow = SDL_CreateWindow(title, 
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  0, 0, // Will be set by fullscreen
                                  SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
#elif defined(__PSP__)
    sdlWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 272, SDL_WINDOW_SHOWN);
#else
    sdlWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale,
                                 DISPLAY_HEIGHT * videoScale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif

    if (sdlWindow == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
#ifdef __ANDROID__
        LOGE("Window creation failed: %s", SDL_GetError());
#endif
        return 1;
    }

#ifdef __ANDROID__
    // Get actual window size for touch control positioning
    int windowW, windowH;
    SDL_GetWindowSize(sdlWindow, &windowW, &windowH);
    LOGI("Window created: %dx%d", windowW, windowH);
    
    // Adjust touch button positions based on actual screen size
    touchDPadCenterX = 120;
    touchDPadCenterY = windowH - 120;
    touchButtonA_X = windowW - 120;
    touchButtonA_Y = windowH - 120;
    touchButtonB_X = windowW - 220;
    touchButtonB_Y = windowH - 220;
#endif

#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
    int mainWindowX;
    int mainWindowWidth;
    SDL_GetWindowPosition(sdlWindow, &mainWindowX, NULL);
    SDL_GetWindowSize(sdlWindow, &mainWindowWidth, NULL);

    int vramWindowX = mainWindowX + mainWindowWidth;
    u16 vramWindowWidth = VRAM_VIEW_WIDTH;
    u16 vramWindowHeight = VRAM_VIEW_HEIGHT;

    vramWindow = SDL_CreateWindow("VRAM View", vramWindowX, SDL_WINDOWPOS_CENTERED, vramWindowWidth, vramWindowHeight,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (vramWindow == NULL) {
        fprintf(stderr, "VRAM Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

#ifdef __PSP__
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (sdlRenderer == NULL)
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
    if (sdlRenderer == NULL)
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
#else
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
#endif

    if (sdlRenderer == NULL) {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
#ifdef __ANDROID__
        LOGE("Renderer creation failed: %s", SDL_GetError());
#endif
        return 1;
    }

#ifdef __ANDROID__
    LOGI("Renderer created successfully");
#endif

#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
    vramRenderer = SDL_CreateRenderer(vramWindow, -1, SDL_RENDERER_PRESENTVSYNC);
    if (vramRenderer == NULL) {
        fprintf(stderr, "VRAM Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

#ifdef __PSP__
    // SDL_RenderSetLogicalSize is broken on PSP, stretch to fill manually
    pspDestRect = (SDL_Rect) { 0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT };
#else
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
    SDL_SetRenderDrawColor(vramRenderer, 0, 0, 0, 255);
    SDL_RenderClear(vramRenderer);
    SDL_RenderSetLogicalSize(vramRenderer, vramWindowWidth, vramWindowHeight);
#endif

    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (sdlTexture == NULL) {
        fprintf(stderr, "Texture could not be created! SDL_Error: %s\n", SDL_GetError());
#ifdef __ANDROID__
        LOGE("Texture creation failed: %s", SDL_GetError());
#endif
        return 1;
    }

#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
    vramTexture = SDL_CreateTexture(vramRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, vramWindowWidth, vramWindowHeight);
    if (vramTexture == NULL) {
        fprintf(stderr, "Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

#if ENABLE_AUDIO
    SDL_AudioSpec want;
    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 48000;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = (want.freq / 60);
    cgb_audio_init(want.freq);
    if (SDL_OpenAudio(&want, 0) < 0) {
        SDL_Log("Failed to open audio: %s", SDL_GetError());
#ifdef __ANDROID__
        LOGE("Audio init failed: %s", SDL_GetError());
#endif
    } else {
        if (want.format != AUDIO_S16) /* we let this one thing change. */
            SDL_Log("We didn't get S16 audio format.");
        SDL_PauseAudio(0);
#ifdef __ANDROID__
        LOGI("Audio initialized successfully");
#endif
    }
#endif

    VDraw(sdlTexture);
#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
    VramDraw(vramTexture);
#endif

#ifdef __ANDROID__
    LOGI("Starting game main loop");
#endif

    AgbMain();
    return 0;
}

bool newFrameRequested = FALSE;

// called every gba frame. we process sdl events and render as many times
// as vsync needs, then return when a new game frame is needed.
void VBlankIntrWait(void)
{
#define HANDLE_VBLANK_INTRS()                                                                                                              \
    ({                                                                                                                                     \
        REG_DISPSTAT |= INTR_FLAG_VBLANK;                                                                                                  \
        RunDMAs(DMA_VBLANK);                                                                                                               \
        if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)                                                                                           \
            gIntrTable[INTR_INDEX_VBLANK]();                                                                                               \
        REG_DISPSTAT &= ~INTR_FLAG_VBLANK;                                                                                                 \
    })

    if (headless) {
        REG_VCOUNT = DISPLAY_HEIGHT + 1;
        HANDLE_VBLANK_INTRS();
        return;
    }

    bool frameAvailable = TRUE;
    bool frameDrawn = false;

    while (isRunning) {
#ifndef __PSP__
        ProcessSDLEvents();
#endif

        if (!paused || stepOneFrame) {
            double dt = fixedTimestep / timeScale;

            if (!newFrameRequested) {
                double deltaTime = 0;
                curGameTime = SDL_GetPerformanceCounter();

                if (stepOneFrame) {
                    deltaTime = dt;
                } else {
                    deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
                    if (deltaTime > (dt * 5))
                        deltaTime = dt * 5;
                }

                lastGameTime = curGameTime;
                accumulator += deltaTime;
            } else {
                newFrameRequested = FALSE;
            }

            while (accumulator >= dt) {
                REG_KEYINPUT = KEYS_MASK ^ Platform_GetKeyInput();

                if (frameAvailable) {
                    VDraw(sdlTexture);
                    frameAvailable = FALSE;
                    frameDrawn = true;

                    HANDLE_VBLANK_INTRS();

                    accumulator -= dt;
                } else {
                    newFrameRequested = TRUE;
                    return;
                }
            }

            if (paused && stepOneFrame) {
                stepOneFrame = false;
            }
        }

        if (frameDrawn) {
            return;
        }

        SDL_Delay(1);
    }
}

void VDraw(SDL_Texture *texture)
{
    uint16_t *pixels;
    int pitch;

    SDL_LockTexture(texture, NULL, (void **)&pixels, &pitch);
    Dma3_32Bit(pixels, (void *)gameImage, (sizeof(gameImage)) / 4);
    SDL_UnlockTexture(texture);

    SDL_RenderClear(sdlRenderer);

#ifdef __PSP__
    SDL_RenderCopy(sdlRenderer, texture, NULL, &pspDestRect);
#else
    SDL_RenderCopy(sdlRenderer, texture, NULL, NULL);
#endif

    SDL_RenderPresent(sdlRenderer);
}

#if ENABLE_VRAM_VIEW && !defined(__ANDROID__)
void VramDraw(SDL_Texture *texture)
{
    uint16_t *pixels;
    int pitch;

    SDL_LockTexture(texture, NULL, (void **)&pixels, &pitch);
    Dma3_32Bit(pixels, (void *)vramBuffer, (sizeof(vramBuffer)) / 4);
    SDL_UnlockTexture(texture);

    SDL_RenderClear(vramRenderer);
    SDL_RenderCopy(vramRenderer, texture, NULL, NULL);
    SDL_RenderPresent(vramRenderer);
}
#else
void VramDraw(SDL_Texture *texture)
{
    // VRAM view disabled on Android
}
#endif

static void ReadSaveFile(char *path)
{
    sSaveFile = fopen(path, "r+b");
    if (sSaveFile == NULL) {
        sSaveFile = fopen(path, "w+b");
    }

    if (sSaveFile == NULL) {
#ifdef __ANDROID__
        LOGE("Failed to open save file: %s", path);
#endif
        // Fill buffer with 0xFF if we can't open the file
        for (int i = 0; i < sizeof(FLASH_BASE); i++) {
            FLASH_BASE[i] = 0xFF;
        }
        return;
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);
    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

#ifdef __ANDROID__
    LOGI("Read %d bytes from save file", bytesRead);
#endif

    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++) {
        FLASH_BASE[i] = 0xFF;
    }
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL) {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
        fflush(sSaveFile);
#ifdef __ANDROID__
        LOGI("Save file written");
#endif
    }
}

void Platform_StoreSaveFile(void) { StoreSaveFile(); }

static void CloseSaveFile()
{
    if (sSaveFile != NULL) {
        fclose(sSaveFile);
    }
}

static u16 keys;

// Key mappings
#define KEY_A_BUTTON      SDLK_c
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_s
#define KEY_R_BUTTON      SDLK_d
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key)                                                                                                                  \
    case KEY_##key:                                                                                                                        \
        keys &= ~key;                                                                                                                      \
        break;

#define HANDLE_KEYDOWN(key)                                                                                                                \
    case KEY_##key:                                                                                                                        \
        keys |= key;                                                                                                                       \
        break;

#ifdef __PSP__
#define BTN_TRIANGLE 0
#define BTN_CIRCLE   1
#define BTN_CROSS    2
#define BTN_SQUARE   3
#define BTN_LTRIGGER 4
#define BTN_RTRIGGER 5
#define BTN_DOWN     6
#define BTN_LEFT     7
#define BTN_UP       8
#define BTN_RIGHT    9
#define BTN_SELECT   10
#define BTN_START    11

static u16 PollJoystickButtons(void)
{
    u16 newKeys = 0;
    if (joystick == NULL)
        return newKeys;

    SDL_JoystickUpdate();

    if (SDL_JoystickGetButton(joystick, BTN_CROSS))
        newKeys |= A_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_CIRCLE))
        newKeys |= B_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_SQUARE))
        newKeys |= B_BUTTON; // Square also B
    if (SDL_JoystickGetButton(joystick, BTN_START))
        newKeys |= START_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_SELECT))
        newKeys |= SELECT_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_LTRIGGER))
        newKeys |= L_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_RTRIGGER))
        newKeys |= R_BUTTON;
    if (SDL_JoystickGetButton(joystick, BTN_UP))
        newKeys |= DPAD_UP;
    if (SDL_JoystickGetButton(joystick, BTN_DOWN))
        newKeys |= DPAD_DOWN;
    if (SDL_JoystickGetButton(joystick, BTN_LEFT))
        newKeys |= DPAD_LEFT;
    if (SDL_JoystickGetButton(joystick, BTN_RIGHT))
        newKeys |= DPAD_RIGHT;

    return newKeys;
}
#endif

#ifdef __ANDROID__
// Android touch input handling
static void HandleTouchInput(SDL_TouchFingerEvent* event)
{
    // Get window size for coordinate conversion
    int windowW, windowH;
    SDL_GetWindowSize(sdlWindow, &windowW, &windowH);
    
    // Convert normalized coordinates (0-1) to pixel coordinates
    float touchX = event->x * windowW;
    float touchY = event->y * windowH;
    
    bool isDown = (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION);
    
    // Check D-Pad (left side)
    float dpadDist = sqrtf((touchX - touchDPadCenterX) * (touchX - touchDPadCenterX) + 
                           (touchY - touchDPadCenterY) * (touchY - touchDPadCenterY));
    if (dpadDist < 100.0f) {
        float angle = atan2f(touchY - touchDPadCenterY, touchX - touchDPadCenterX) * 180.0f / 3.14159f;
        virtualButtonsPressed[6] = isDown && (angle > -135 && angle < -45);  // UP
        virtualButtonsPressed[7] = isDown && (angle > 45 && angle < 135);    // DOWN
        virtualButtonsPressed[8] = isDown && (angle < -135 || angle > 135);  // LEFT
        virtualButtonsPressed[9] = isDown && (angle > -45 && angle < 45);    // RIGHT
    } else if (event->type == SDL_FINGERUP) {
        virtualButtonsPressed[6] = virtualButtonsPressed[7] = 
        virtualButtonsPressed[8] = virtualButtonsPressed[9] = false;
    }
    
    // Check A button
    float distA = sqrtf((touchX - touchButtonA_X) * (touchX - touchButtonA_X) + 
                        (touchY - touchButtonA_Y) * (touchY - touchButtonA_Y));
    if (distA < 60.0f) {
        virtualButtonsPressed[0] = isDown; // A
    }
    
    // Check B button
    float distB = sqrtf((touchX - touchButtonB_X) * (touchX - touchButtonB_X) + 
                        (touchY - touchButtonB_Y) * (touchY - touchButtonB_Y));
    if (distB < 60.0f) {
        virtualButtonsPressed[1] = isDown; // B
    }
    
    // Start/Select in top corners
    if (touchY < 100) {
        if (touchX < 100) {
            virtualButtonsPressed[4] = isDown; // L
        } else if (touchX > windowW - 100) {
            virtualButtonsPressed[5] = isDown; // R
        }
        
        if (touchX > 100 && touchX < 200) {
            virtualButtonsPressed[2] = isDown; // Start
        } else if (touchX > 200 && touchX < 300) {
            virtualButtonsPressed[3] = isDown; // Select
        }
    }
}
#endif

void ProcessSDLEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            isRunning = false;
            break;

#ifdef __ANDROID__
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            HandleTouchInput(&event.tfinger);
            break;
            
        case SDL_APP_WILLENTERBACKGROUND:
            LOGI("App entering background - pausing");
            paused = true;
            StoreSaveFile();
            break;
            
        case SDL_APP_DIDENTERFOREGROUND:
            LOGI("App entering foreground - resuming");
            paused = false;
            break;
            
        case SDL_APP_LOWMEMORY:
            LOGI("Low memory warning");
            break;
#endif

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                HANDLE_KEYDOWN(A_BUTTON);
                HANDLE_KEYDOWN(B_BUTTON);
                HANDLE_KEYDOWN(START_BUTTON);
                HANDLE_KEYDOWN(SELECT_BUTTON);
                HANDLE_KEYDOWN(L_BUTTON);
                HANDLE_KEYDOWN(R_BUTTON);
                HANDLE_KEYDOWN(DPAD_UP);
                HANDLE_KEYDOWN(DPAD_DOWN);
                HANDLE_KEYDOWN(DPAD_LEFT);
                HANDLE_KEYDOWN(DPAD_RIGHT);
            case SDLK_ESCAPE:
                isRunning = false;
                break;
            case SDLK_p:
                paused = !paused;
                break;
            case SDLK_o:
                if (paused)
                    stepOneFrame = true;
                break;
            case SDLK_LSHIFT:
                speedUp = true;
                break;
            default:
                break;
            }
            break;

        case SDL_KEYUP:
            switch (event.key.keysym.sym) {
                HANDLE_KEYUP(A_BUTTON);
                HANDLE_KEYUP(B_BUTTON);
                HANDLE_KEYUP(START_BUTTON);
                HANDLE_KEYUP(SELECT_BUTTON);
                HANDLE_KEYUP(L_BUTTON);
                HANDLE_KEYUP(R_BUTTON);
                HANDLE_KEYUP(DPAD_UP);
                HANDLE_KEYUP(DPAD_DOWN);
                HANDLE_KEYUP(DPAD_LEFT);
                HANDLE_KEYUP(DPAD_RIGHT);
            case SDLK_LSHIFT:
                speedUp = false;
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
    }
}

u16 Platform_GetKeyInput(void)
{
    u16 currentKeys = keys;

#ifdef __PSP__
    currentKeys |= PollJoystickButtons();
#endif

#ifdef __ANDROID__
    // Add virtual button states
    if (virtualButtonsPressed[0]) currentKeys |= A_BUTTON;
    if (virtualButtonsPressed[1]) currentKeys |= B_BUTTON;
    if (virtualButtonsPressed[2]) currentKeys |= START_BUTTON;
    if (virtualButtonsPressed[3]) currentKeys |= SELECT_BUTTON;
    if (virtualButtonsPressed[4]) currentKeys |= L_BUTTON;
    if (virtualButtonsPressed[5]) currentKeys |= R_BUTTON;
    if (virtualButtonsPressed[6]) currentKeys |= DPAD_UP;
    if (virtualButtonsPressed[7]) currentKeys |= DPAD_DOWN;
    if (virtualButtonsPressed[8]) currentKeys |= DPAD_LEFT;
    if (virtualButtonsPressed[9]) currentKeys |= DPAD_RIGHT;
#endif

    return currentKeys;
}
