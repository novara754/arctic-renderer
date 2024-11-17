#pragma once

#include <spdlog/spdlog.h>

#define DXERR(x, msg)                                                                              \
    if (FAILED(x))                                                                                 \
    {                                                                                              \
        spdlog::error("{}: 0x{:x}", msg, static_cast<unsigned long>(x));                           \
        return false;                                                                              \
    }
