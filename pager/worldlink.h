// Following a byond:// link to another world. The new run is this run's command line with only the target replaced,
// so it plays as the same account with the same identity file, state dir and options.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace fugue::pager {

// args, hostTaken: as for parseDsArgv. Returns the arguments after argv[0], led by the `client` subcommand unless
// asDreamSeeker. nullopt with *err when url is not a host:port address (parseTarget refuses hub world names) or the
// new run would not play as this one does.
std::optional<std::vector<std::string>> worldLinkArgs(const std::vector<std::string>& args, const std::vector<bool>& hostTaken,
                                                      bool asDreamSeeker, const std::string& url, std::string* err = nullptr);

}  // namespace fugue::pager
