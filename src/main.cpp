#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <spdlog/spdlog.h>

#include "app.hpp"

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::trace);

    if (argc != 2)
    {
        spdlog::error("main: usage: arctic <scene>");
        return false;
    }

    SDL_SetAppMetadata("Arctic", "0.1", nullptr);
    spdlog::trace("main: set sdl app metadata");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        spdlog::error("main: failed to initialize sdl: {}", SDL_GetError());
        return 1;
    }
    spdlog::trace("main: initialized sdl video and audio subsystem");

    SDL_Window *window =
        SDL_CreateWindow("Arctic", Arctic::App::WINDOW_WIDTH, Arctic::App::WINDOW_HEIGHT, 0);
    if (!window)
    {
        spdlog::error("main: failed to create window: {}", SDL_GetError());
        return 1;
    }
    spdlog::trace("main: created sdl window");

    try
    {
        Arctic::App app(window, argv[1]);
        if (app.init())
        {
            spdlog::trace("main: initialized app");
            spdlog::trace("main: running app");
            app.run();
            spdlog::trace("main: app has exitted");
        }
        else
        {
            spdlog::error("main: failed to initialize app");
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("main: app threw exception: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("main: app threw unknown exception");
    }

    SDL_DestroyWindow(window);
    spdlog::trace("main: process terminating...");
    return 0;
}
