/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project (https://github.com/Warzone2100)

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file sdl_gamepad_script.h
 * Debug aid: plays a script of gamepad input on an SDL virtual gamepad.
 *
 * WZ_DEBUG_GAMEPAD_SCRIPT=<file> attaches a virtual gamepad when the gamepad subsystem starts, and
 * plays the script on it, one step each frame, so that a test can drive the game with a gamepad
 * alone (the Vulkan SC build takes no keyboard or mouse input). The game sees an ordinary gamepad.
 *
 * One command per line; '#' starts a comment:
 *   wait <frames>                        do nothing for that many frames
 *   waitms <milliseconds>                do nothing for that long
 *   press <button> [<frames>]            hold the button (4 frames by default), then release it for 4
 *   flick <up|down|left|right>           a quick push of the left stick: the cursor hops to the next widget.
 *                                        The game takes it as a flick only if it is released within 180 ms:
 *                                        on a slow device, use moveto
 *   stick <left|right> <x> <y> <frames>  hold a stick at x, y (-1 to 1), then center it
 *   moveto <x> <y>                       steer the cursor with the left stick to that screen position (the
 *                                        game's interface pixels); after 10 seconds the script stops, and
 *                                        the game quits with exit code 1
 *   screenshot <label>                   save a screenshot (screenshots/ in the config directory)
 *   log <text>                           write the text to the log
 *   fail <text>                          write the text to the log as an error, end the script, and quit
 *                                        with exit code 1
 *   quit                                 end the script, and ask the game to quit
 * Buttons: a b x y back guide start leftstick rightstick leftshoulder rightshoulder up down left right
 */

#pragma once

void wzGamepadScriptInit();
void wzGamepadScriptUpdate();
void wzGamepadScriptShutdown();
