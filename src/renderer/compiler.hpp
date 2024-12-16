#pragma once

#include <vector>

#include <Windows.h>
#include <atlbase.h>
#include <dxcapi.h>

#include "comptr.hpp"

namespace Arctic::Renderer
{

class Compiler
{
    ComPtr<IDxcUtils> m_utils;
    ComPtr<IDxcCompiler3> m_compiler;
    ComPtr<IDxcIncludeHandler> m_include_handler;

    Compiler(const Compiler &) = delete;
    Compiler &operator=(const Compiler &) = delete;
    Compiler(Compiler &&) = delete;
    Compiler &operator=(Compiler &&) = delete;

  public:
    Compiler() = default;

    [[nodiscard]] bool init();

    [[nodiscard]] bool compile_shader(
        LPCWSTR path, LPCWSTR entry_point, LPCWSTR target, std::vector<uint8_t> &code
    ) const;
};

} // namespace Arctic::Renderer
