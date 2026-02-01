#define _USE_MATH_DEFINES

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>

#include <Eigen/Dense>
using namespace Eigen;

#include <vector>
#include <iostream>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

static constexpr int WINDOW_WIDTH   = 800;
static constexpr int WINDOW_HEIGHT  = 600;
static constexpr double VIEW_WIDTH  = 1.0 * 800.0f;
static constexpr double VIEW_HEIGHT = 1.0 * 600.0f;

SDL_Window* window     = nullptr;
SDL_Renderer* renderer = nullptr;
bool isRunning         = true;

// solver parameters
static const Vector2d G(0.0f, 10.0f);        // external (gravitational) forces
static constexpr float REST_DENS = 300.0f;   // rest density
static constexpr float GAS_CONST = 2000.0f;  // const for equation of state
static constexpr float H         = 16.0f;    // kernel radius
static constexpr float HSQ       = H * H;    // radius^2 for optimization
static constexpr float MASS      = 2.5f;     // assume all particles have the same mass
static constexpr float VISC      = 200.0f;   // viscosity constant
static constexpr float DT        = 0.0007f;  // integration timestep

// smoothing kernels defined in Müller and their gradients
const static float POLY6      = 4.f / (M_PI * std::pow(H, 8.f));
const static float SPIKY_GRAD = -10.f / (M_PI * std::pow(H, 5.f));
const static float VISC_LAP   = 40.f / (M_PI * std::pow(H, 5.f));

// simulation parameters
static constexpr float EPS           = H;  // boundary epsilon
static constexpr float BOUND_DAMPING = -0.5f;

/**
 * particle data structure
 * stores position, velocity, and force for integration
 * stores density(rho) and pressure values for SPH
 */
struct Particle
{
    Particle(float x, float y) :
        position(x, y), velocity(0.0f, 0.0f), force(0.0f, 0.0f), density(0.0f), pressure(0.0f) {};

    Vector2d position;
    Vector2d velocity;
    Vector2d force;
    float density;
    float pressure;
};

// solver data
static std::vector<Particle> particles;

// Cells
static const uint32_t CELL_NX = (uint32_t)std::ceil(VIEW_WIDTH / H);
static const uint32_t CELL_NY = (uint32_t)std::ceil(VIEW_HEIGHT / H);
static std::vector<std::vector<uint32_t>> cells;

// interaction
static constexpr int MAX_PARTICLES   = 2500;
static constexpr int DAM_PARTICLES   = 500;
static constexpr int BLOCK_PARTICLES = 250;

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

// Cells
void BuildCells();
uint32_t CellPositionToId(uint32_t ix, uint32_t iy);
std::vector<uint32_t> Neighbors(Particle& particle);

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
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    for (auto& particle : particles)
    {
        filledCircleRGBA(renderer,
                         particle.position[0],
                         particle.position[1],
                         H / 2,
                         0.2f * 255,
                         0.6f * 255,
                         255,
                         255);
    }
    SDL_RenderPresent(renderer);
}

void Shutdown()
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

void InitSPH()
{
    std::cout << "initializing dam break with " << DAM_PARTICLES << " particles" << std::endl;

    for (float y = EPS; y < VIEW_HEIGHT - EPS * 2.0f; y += H)
    {
        for (float x = VIEW_WIDTH / 4; x <= VIEW_WIDTH / 2; x += H)
        {
            if (particles.size() >= DAM_PARTICLES)
            {
                return;
            }
            float jitter = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
            particles.push_back(Particle(x + jitter, y));
        }
    }
}

void Integrate()
{
    for (auto& particle : particles)
    {
        // forward Euler integration
        particle.velocity += DT * particle.force / particle.density;
        particle.position += DT * particle.velocity;

        // enforce boundary conditions
        if (particle.position(0) - EPS < 0.0f)
        {
            particle.velocity(0) *= BOUND_DAMPING;
            particle.position(0) = EPS;
        }
        if (particle.position(0) + EPS > VIEW_WIDTH)
        {
            particle.velocity(0) *= BOUND_DAMPING;
            particle.position(0) = VIEW_WIDTH - EPS;
        }
        if (particle.position(1) - EPS < 0.0f)
        {
            particle.velocity(1) *= BOUND_DAMPING;
            particle.position(1) = EPS;
        }
        if (particle.position(1) + EPS > VIEW_HEIGHT)
        {
            particle.velocity(1) *= BOUND_DAMPING;
            particle.position(1) = VIEW_HEIGHT - EPS;
        }
    }
}

void ComputeDensityPressure()
{
    for (auto& pi : particles)
    {
        pi.density = 0.0f;
        for (uint32_t neighborId : Neighbors(pi))
        {
            auto& pj     = particles[neighborId];
            Vector2d rij = pj.position - pi.position;
            float r2     = rij.squaredNorm();

            if (r2 < HSQ)
            {
                // this computation is symmetric
                pi.density += MASS * POLY6 * std::pow(HSQ - r2, 3.0f);
            }
        }
        pi.pressure = GAS_CONST * (pi.density - REST_DENS);
    }
}

void ComputeForces()
{
    for (auto& pi : particles)
    {
        Vector2d fpress(0.0f, 0.0f);
        Vector2d fvisc(0.0f, 0.0f);

        for (uint32_t neighborId : Neighbors(pi))
        {
            auto& pj = particles[neighborId];
            if (&pi == &pj)
            {
                continue;
            }

            Vector2d rij = pj.position - pi.position;
            float r      = rij.norm();

            if (r < H)
            {
                // compute pressure force contribution
                fpress += -rij.normalized() * MASS * (pi.pressure + pj.pressure)
                          / (2.0f * pj.density) * SPIKY_GRAD * std::pow(H - r, 3.0f);
                // compute viscosity force contribution
                fvisc +=
                    VISC * MASS * (pj.velocity - pi.velocity) / pj.density * VISC_LAP * (H - r);
            }
        }
        Vector2d fgrav = G * MASS / pi.density;
        pi.force       = fpress + fvisc + fgrav;
    }
}

void Update()
{
    BuildCells();
    ComputeDensityPressure();
    ComputeForces();
    Integrate();
}

void BuildCells()
{
    cells.clear();
    cells.resize(CELL_NX * CELL_NY);

    for (uint32_t i = 0; i < particles.size(); ++i)
    {
        auto& particle  = particles[i];
        uint32_t ix     = (uint32_t)(particle.position(0) / H);
        uint32_t iy     = (uint32_t)(particle.position(1) / H);
        uint32_t cellId = CellPositionToId(ix, iy);
        cells[cellId].push_back(i);
    }
}

uint32_t CellPositionToId(uint32_t ix, uint32_t iy)
{
    return CELL_NX * iy + ix;
}

std::vector<uint32_t> Neighbors(Particle& particle)
{
    uint32_t ix = (uint32_t)(particle.position(0) / H);
    uint32_t iy = (uint32_t)(particle.position(1) / H);

    std::vector<uint32_t> result;
    for (auto dx : {-1, 0, 1})
    {
        for (auto dy : {-1, 0, 1})
        {
            uint32_t jx = ix + dx;
            uint32_t jy = iy + dy;
            if (jx >= 0 && jx < CELL_NX && jy >= 0 && jy < CELL_NY)
            {
                uint32_t neighborId                    = CellPositionToId(jx, jy);
                std::vector<uint32_t>& neighborsInCell = cells[neighborId];
                result.insert(result.end(), neighborsInCell.begin(), neighborsInCell.end());
            }
        }
    }
    return result;
}

int main(int argc, char* argv[])
{
    InitSDL();
    InitSPH();

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

        Update();
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
