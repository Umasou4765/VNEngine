#pragma once

// Optional Tracy integration with safe no-op fallback.
#if defined(__has_include)
#if __has_include(<tracy/Tracy.hpp>)
#include <tracy/Tracy.hpp>
#elif __has_include("tracy/Tracy.hpp")
#include "tracy/Tracy.hpp"
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#endif
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#endif
