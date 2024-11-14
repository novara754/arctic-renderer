cmake_minimum_required(VERSION 3.11)
project(arctic)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SDL_STATIC ON)
set(SDL_SHARED OFF)

FetchContent_Declare(
        spdlog
        SYSTEM
        GIT_REPOSITORY "https://github.com/gabime/spdlog"
        GIT_TAG "b6da59447f165ad70a4e3ca1c575b14ea66d92c9"
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        SDL3
        SYSTEM
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL"
        GIT_TAG "68cc173d9293e890ba93feaed1d3dc17742aa9b3"
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
        imgui
        SYSTEM
        GIT_REPOSITORY "https://github.com/ocornut/imgui.git"
        GIT_TAG "71c77c081ac36841e682498229088e7678207112"
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
        glm
        SYSTEM
        GIT_REPOSITORY "https://github.com/g-truc/glm"
        GIT_TAG "1.0.1"
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
        DirectX-Headers
        SYSTEM
        GIT_REPOSITORY "https://github.com/microsoft/DirectX-Headers"
        GIT_TAG "v1.0"
)
FetchContent_MakeAvailable(DirectX-Headers)

add_executable(arctic
        src/main.cpp
        src/app.cpp

        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp

        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_compile_options(arctic PRIVATE /W4 /WX)

target_compile_definitions(arctic PRIVATE
        _CRT_SECURE_NO_WARNINGS
        GLM_FORCE_EXPLICIT_CTOR
)
target_include_directories(arctic PRIVATE ${imgui_SOURCE_DIR})
target_include_directories(arctic PRIVATE ${imgui_SOURCE_DIR}/backends)
target_include_directories(arctic PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(arctic PRIVATE DirectX-Headers)
target_link_libraries(arctic PRIVATE spdlog::spdlog)
target_link_libraries(arctic PRIVATE SDL3::SDL3-static)
target_link_libraries(arctic PRIVATE glm::glm)
target_link_libraries(arctic PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
