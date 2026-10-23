#pragma once

// Printf-style logging routed through SDL (logcat on Android, the Xcode console on Apple).
namespace nova::log {

#if defined(__GNUC__) || defined(__clang__)
#define NOVA_PRINTF_FORMAT(fmtIndex, argIndex) __attribute__((format(printf, fmtIndex, argIndex)))
#else
#define NOVA_PRINTF_FORMAT(fmtIndex, argIndex)
#endif

void info(const char* fmt, ...) NOVA_PRINTF_FORMAT(1, 2);
void warn(const char* fmt, ...) NOVA_PRINTF_FORMAT(1, 2);
void error(const char* fmt, ...) NOVA_PRINTF_FORMAT(1, 2);

} // namespace nova::log
