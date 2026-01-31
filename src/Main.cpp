#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

static constexpr int WINDOW_WIDTH  = 1024;
static constexpr int WINDOW_HEIGHT = 768;

SDL_Window* window     = nullptr;
SDL_Renderer* renderer = nullptr;
bool isRunning         = true;

// SDL
void InitSDL();
void Render();
void Shutdown();

// Solver
void InitSPH();
void Integrate();
void ComputeDensityPressure();
void ComputeForces();
void Update();

// Interactivity
void Keyboard(SDL_Scancode code);

void InitSDL()
{
    int sdlResult = SDL_Init(SDL_INIT_VIDEO);
    if (sdlResult != 0)
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow("SphSample",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH,
                              WINDOW_HEIGHT,
                              0);
    if (!window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        exit(1);
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        exit(1);
    }
}

void Render()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    filledCircleRGBA(renderer, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2, 100, 0, 0, 255, 255);
    SDL_RenderPresent(renderer);
}

void Shutdown()
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

void Update()
{
    ComputeDensityPressure();
    ComputeForces();
    Integrate();
}

int main(int argc, char* argv[])
{
    InitSDL();

    auto mainLoop = []()
    {
#ifdef __EMSCRIPTEN__
        if (!isRunning)
        {
            emscripten_cancel_main_loop();
            Shutdown();
            return;
        }
#endif

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    isRunning = false;
                    break;

                default:
                    break;
            }
        }

        auto state = SDL_GetKeyboardState(NULL);
        if (state[SDL_SCANCODE_ESCAPE])
        {
            isRunning = false;
        }

        Render();
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoop, 0, 1);
#else
    while (isRunning)
    {
        mainLoop();
    }
    Shutdown();
#endif

    return 0;
}
