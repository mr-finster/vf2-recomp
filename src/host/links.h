// The game's own links, from the Online page on the web (vf3-recomp's tools/online): vf2recomp://vf2/join/KXQ4M7 opens
// the game straight into that room (Online > Enter code's way: joined if it's open, watched if a match is on),
// .../watch/CODE into watching it. Each game its own scheme (vf1recomp, vf2recomp, vf3recomp), so all three can be
// installed side by side. A link is a stranger's text: only exactly that form is taken, the code checked
// letter by letter, and nothing else in it ever read or acted on.
//
// How a link reaches the game: on a Mac, the app's Info.plist says it opens vf2recomp: (tools/mac/package.sh), and
// the system hands the link to the running game, or starts it with it, as an open-URL event (SDL's drop event). On
// Windows and Linux the game says so itself, for this user only (register: the registry's HKCU, a .desktop file), and
// a link starts the game with it as its argument; with a copy already running, the new copy hands it to that one
// through the data folder (hand_over / take) and quits at once. (Android: none of it; the links aren't offered there.)
#pragma once
#include <string>

namespace links {

constexpr const char *SCHEME = "vf2recomp";
struct Link { std::string game, action, code; };   // game "vf2", action "join" or "watch", the code's 6 letters

bool parse(const std::string &url, Link &out);
bool is_link(const std::string &arg);   // (starts with the scheme: what's to be parsed, not taken for a file)
// This user's system told this program opens the links (Windows, Linux; nothing on a Mac): again only when it's
// changed (a program moved). Returns what was done, for the log ("" nothing)
std::string register_scheme();
// A link for the copy already running (Windows, Linux): true if there's one and it has it now (this one quits)
bool hand_over(const std::string &data, const std::string &url);
void running(const std::string &data);       // (this copy is the one running: links handed to it)
void stopped(const std::string &data);
bool take(const std::string &data, std::string &url);   // (a link handed to this copy, now and then)

} // namespace links
