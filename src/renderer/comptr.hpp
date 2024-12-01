#pragma once

#include <wrl.h>

namespace Arctic::Renderer
{

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

} // namespace Arctic::Renderer
