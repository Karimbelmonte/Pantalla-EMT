#pragma once
#include <cstddef>
#include <cstring>
inline void esp_fill_random(void* buffer, size_t size) { std::memset(buffer, 0xAB, size); }
