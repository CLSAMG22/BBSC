// BBSC mod: stand-in for the emulator's logging. Same macros, written to bbsc-mod.log.
#pragma once
#include <string>
#include <fmt/format.h>

namespace BbscMod {
void LogLine(char level, const char* category, const std::string& text);
}

#define LOG_TRACE(cat, ...) ::BbscMod::LogLine('T', #cat, fmt::format(__VA_ARGS__))
#define LOG_DEBUG(cat, ...) ::BbscMod::LogLine('D', #cat, fmt::format(__VA_ARGS__))
#define LOG_INFO(cat, ...) ::BbscMod::LogLine('I', #cat, fmt::format(__VA_ARGS__))
#define LOG_WARNING(cat, ...) ::BbscMod::LogLine('W', #cat, fmt::format(__VA_ARGS__))
#define LOG_ERROR(cat, ...) ::BbscMod::LogLine('E', #cat, fmt::format(__VA_ARGS__))
#define LOG_CRITICAL(cat, ...) ::BbscMod::LogLine('C', #cat, fmt::format(__VA_ARGS__))
