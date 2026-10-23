/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/** @file
 *  The automatic camera. It moves the view to the place with the most fighting.
 *  The --autocamera option enables it, to watch an --autogame battle.
 */

#ifndef __INCLUDED_SRC_AUTOCAMERA_H__
#define __INCLUDED_SRC_AUTOCAMERA_H__

/// Moves the camera when the fighting moves. Call it once per rendered frame, in the game.
/// It does nothing when --autocamera is not set. It changes only the view, not the simulation.
void autoCameraUpdate();

#endif // __INCLUDED_SRC_AUTOCAMERA_H__
