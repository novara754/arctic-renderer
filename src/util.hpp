#pragma once

#include <cstdint>

static inline uint32_t next_multiple_of_k(uint32_t n, uint32_t k)
{
    if (n % k == 0)
    {
        return n;
    }

    return (n / k + 1) * k;
}
