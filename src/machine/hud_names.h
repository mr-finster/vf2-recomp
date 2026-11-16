// The players' names under the energy bars, in a netplay match: each player's own name (up to 10 letters) where the
// fight's HUD has its fighter's. Drawn into the 2D picture over the 3D as it's made (model2a::draw_2d), never into the
// board: the game's state is the same whatever the names, so they can't cause a desync, and a rollback can't leave
// one half drawn. VF3's hud_names, for VF2's HUD.
//
// The HUD (map 0, rows 6 and 7) has each fighter's name as a picture, 6 tiles by 2 (a box: a black frame, a checker
// of black and see-through, the letters in orange, a light line under it): player 1's at columns 2-7, player 2's at
// 54-59; its round markers, two tiles side by side each, at columns 22-25 and 36-39 (more for more rounds). Where
// they're there and the map unscrolled, each box is made again as wide as the name (from the game's own box: its
// edges and its checker) and the name drawn in it: player 1's from the left, player 2's to the right.
#pragma once
#include "mame_lite.h"
#include <cstdint>
#include <string>
#include <vector>

class segas24_tile;

namespace m2hud {

// A country's flag beside a name, out from the middle, where it fits: its size in the picture's pixels, and those
// pixels (0xAARRGGBB), or none: its place left in the box for the host to draw it there, sharper (flag_at)
struct Flag { int w = 0, h = 0; std::vector<uint32_t> px; };

// top: the layers over the 3D (model2a's), the arcade's column 0 at ox. False if the HUD wasn't there. flag_at: each
// player's flag's place, its left and the box's top (the flag 3.5 lower, in the middle of the letters' rows), in top's
// pixels; -1 for none drawn
// tags: a line under each box ("" none; a Rooms 2 room's run of wins, "3 WINS"), in the same letters, out from the middle
bool draw(const segas24_tile &tiles, bitmap_rgb32 &top, int ox, const std::string names[2], const Flag *flags[2] = nullptr, int flag_at[2][2] = nullptr,
          const std::string *tags = nullptr);

// The game's own CHAMPION n WIN(S) over the energy bars, on side 0 (player 1's, left of the timer) or 1: map 0's rows
// 1 and 2 (its letters two tiles tall), from the C of CHAMPION (found by its top row's tiles) to the line's last
// letter; c0, c1 its first and last columns. False if it isn't there. (A Rooms 2 room's match hides it: the room's
// run of wins is under the name, model2a::set_hide_champion)
bool champion_at(const segas24_tile &tiles, int side, int &c0, int &c1);
// The CHALLENGER over the other bar, on side 0 or 1: only while CHAMPION is over the other side's, this side's rows 1
// and 2 from their first lit column to the last (hidden with it in a Rooms 2 room's match)
bool challenger_at(const segas24_tile &tiles, int side, int &c0, int &c1);

} // namespace m2hud
