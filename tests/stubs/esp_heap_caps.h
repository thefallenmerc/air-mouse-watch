#pragma once
#include <cstdlib>
inline void *ps_malloc(size_t n) { return std::malloc(n); }
