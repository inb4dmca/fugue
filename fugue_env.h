// Environment lookup for the client's own settings. Every setting is FUGUE_<NAME>.
#pragma once
#include <cstdlib>
#include <optional>
#include <string>

namespace fugue {

// FUGUE_<name>; nullopt when unset. `name` is the suffix, e.g. "AUDIO".
inline std::optional<std::string> fugueEnv(const std::string& name) {
    if (const char* v = std::getenv(("FUGUE_" + name).c_str())) return std::string(v);
    return std::nullopt;
}

// As fugueEnv, but unset and empty both read as "".
inline std::string fugueEnvOr(const std::string& name) { return fugueEnv(name).value_or(std::string()); }

}  // namespace fugue
