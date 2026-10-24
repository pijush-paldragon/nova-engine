#pragma once

// Test support for the UI: log capture and (from later tasks on) headless services.

#include <SDL3/SDL_log.h>

#include <string>
#include <vector>

namespace nova::testing {

// Collects every SDL log line while alive (nova::log routes through SDL).
class LogCapture {
public:
    LogCapture() {
        SDL_GetLogOutputFunction(&prevFn_, &prevData_);
        SDL_SetLogOutputFunction(&LogCapture::record, this);
    }
    ~LogCapture() { SDL_SetLogOutputFunction(prevFn_, prevData_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    const std::vector<std::string>& lines() const { return lines_; }
    int count(const std::string& needle) const {
        int n = 0;
        for (const std::string& l : lines_) n += l.find(needle) != std::string::npos;
        return n;
    }
    void clear() { lines_.clear(); }

private:
    static void SDLCALL record(void* self, int, SDL_LogPriority, const char* message) {
        static_cast<LogCapture*>(self)->lines_.emplace_back(message);
    }
    SDL_LogOutputFunction prevFn_ = nullptr;
    void* prevData_ = nullptr;
    std::vector<std::string> lines_;
};

} // namespace nova::testing
