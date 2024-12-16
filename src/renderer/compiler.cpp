#include "compiler.hpp"

#include "dxerr.hpp"

namespace Arctic::Renderer
{

bool Compiler::init()
{
    DXERR(
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils)),
        "Compiler::init: failed to create utils"
    );

    DXERR(
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler)),
        "Compiler::init: failed to create compiler"
    );

    // m_utils->CreateDefaultIncludeHandler(&m_include_handler);

    return true;
}

bool Compiler::compile_shader(
    LPCWSTR path, LPCWSTR entry_point, LPCWSTR target, std::vector<uint8_t> &code
) const
{
    ComPtr<IDxcBlobEncoding> source_file;
    DXERR(
        m_utils->LoadFile(path, nullptr, &source_file),
        "Compiler::compile_shader: failed to load shader source file"
    );

    DxcBuffer source_code;
    source_code.Ptr = source_file->GetBufferPointer();
    source_code.Size = source_file->GetBufferSize();
    source_code.Encoding = DXC_CP_ACP; // auto-detect

    LPCWSTR compiler_args[] = {
        path,
        L"-E",
        entry_point,
        L"-T",
        target,
        // L"-HV",
        // L"2021"
        // L"-Zi", // enable debug info
    };

    ComPtr<IDxcResult> results;
    m_compiler->Compile(
        &source_code,
        compiler_args,
        _countof(compiler_args),
        nullptr,
        IID_PPV_ARGS(&results)
    );

    HRESULT compile_status;
    results->GetStatus(&compile_status);
    if (FAILED(compile_status))
    {
        ComPtr<IDxcBlobUtf8> errors;
        results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors != nullptr && errors->GetStringLength() != 0)
        {
            spdlog::error(
                "Compiler::compile_shader: failed to compile shader:\n{}",
                errors->GetStringPointer()
            );
        }
        else
        {
            spdlog::error("Compiler::compile_shader: failed to compile shader");
        }
        return false;
    }

    ComPtr<IDxcBlob> compiled_shader;
    results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&compiled_shader), nullptr);
    if (compiled_shader == nullptr)
    {
        spdlog::error("Compiler::compile_shader: failed to get compiled shader code");
        return false;
    }

    code = std::vector<uint8_t>(
        static_cast<uint8_t *>(compiled_shader->GetBufferPointer()),
        static_cast<uint8_t *>(compiled_shader->GetBufferPointer()) +
            compiled_shader->GetBufferSize()
    );

    return true;
}

} // namespace Arctic::Renderer
