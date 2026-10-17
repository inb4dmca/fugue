// Audio status lines: the chosen sink and why earlier candidates failed, refused decodes, reopens, and the periodic
// stats line. They go to stderr and to a file, since a client started from a desktop menu has no terminal.
// The file is <state-dir>/audio.log for an explicit --state-dir, else ~/.fugue/audio.log. It is truncated at each
// session start, so it describes the last session.
#pragma once
#include <string>

namespace fugue::audio {

// "" = stderr only (the default, and every unit test). Thread-safe; returns false (stderr only) when it cannot be opened.
bool setNoteFile(const std::string& path, std::string* err = nullptr);
std::string noteFile();
// "[audio] <line>" to stderr, and "<local time> <line>" to the note file (flushed per line: the client may be killed).
void note(const std::string& line);

}  // namespace fugue::audio
