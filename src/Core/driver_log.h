// driver_log.h — minimal logging shim used only by driver_controller.cpp.
// The cs2 project doesn't pull in DayZ-External's log.h / TAG enum, so we
// provide local LOG_INFO / LOG_ERROR / TAG_DRV macros that route through
// OutputDebugStringA. Same call-sites work; output goes to DbgView.
#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstdarg>

enum { TAG_DRV = 0 };

inline void GhaxDrvLog_(const char* lvl, const char*, const char* fmt, ...)
{
    char buf[512];
    int n = _snprintf_s(buf, _TRUNCATE, "[%s][DRV] ", lvl);
    if (n < 0) n = 0;
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    size_t L = strnlen_s(buf, sizeof(buf));
    if (L + 2 < sizeof(buf)) { buf[L] = '\n'; buf[L + 1] = 0; }
    OutputDebugStringA(buf);
}

#define LOG_INFO(tag,  ...) GhaxDrvLog_("INFO",  "DRV", __VA_ARGS__)
#define LOG_ERROR(tag, ...) GhaxDrvLog_("ERROR", "DRV", __VA_ARGS__)
