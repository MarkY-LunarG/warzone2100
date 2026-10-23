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
 *  The automatic camera for --autogame battles.
 *
 *  Every scan, the camera divides the map into square cells and gives each cell
 *  an action score from the objects in it. A droid that attacks, a droid or a
 *  structure that was hit a short time ago, and a droid that moves all add to
 *  the score. The camera then slides to the center of the 3x3 cell area with the
 *  highest score. It stays at a place for a minimum time, and it does not move
 *  while the current place still has most of the action. This stops the view
 *  from jumping between two fights.
 *
 *  The scan uses only the display clock (realTime) and only reads the game
 *  state, so it has no effect on the simulation or the sync CRCs.
 */

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"

#include "autocamera.h"
#include "basedef.h"
#include "clparse.h"
#include "display.h"
#include "display3d.h"
#include "droiddef.h"
#include "game_world.h"
#include "map.h"
#include "structuredef.h"
#include "warcam.h"

#include <vector>

namespace
{

/// The width of one score cell, in tiles.
constexpr int CELL_TILES = 6;
constexpr int CELL_UNITS = CELL_TILES * TILE_UNITS;
/// Real time between two scans, in milliseconds.
constexpr unsigned SCAN_INTERVAL_MS = 250;
/// Minimum real time that the camera stays at a place, in milliseconds.
constexpr unsigned HOLD_MS = 4000;
/// Game time after a hit in which an object still counts as "under attack", in milliseconds.
constexpr unsigned RECENT_HIT_MS = 3000;
/// The camera does not move for a target that is closer than this, in world units.
constexpr int MIN_MOVE_UNITS = 3 * TILE_UNITS;
/// The camera stays when the current place has at least this percentage of the best score.
constexpr int STAY_PERCENT = 60;

constexpr int SCORE_ATTACK = 3;
constexpr int SCORE_HIT = 3;
constexpr int SCORE_STRUCTURE_HIT = 4;
constexpr int SCORE_MOVE = 1;

struct Cell
{
	int score = 0;
	int64_t sumX = 0;
	int64_t sumY = 0;
};

struct ScoreGrid
{
	int cellsX = 0;
	int cellsY = 0;
	std::vector<Cell> cells;

	void add(const Vector3i &pos, int score)
	{
		const int cx = std::min(std::max(pos.x / CELL_UNITS, 0), cellsX - 1);
		const int cy = std::min(std::max(pos.y / CELL_UNITS, 0), cellsY - 1);
		Cell &cell = cells[cy * cellsX + cx];
		cell.score += score;
		cell.sumX += static_cast<int64_t>(pos.x) * score;
		cell.sumY += static_cast<int64_t>(pos.y) * score;
	}

	/// The sum of the 3x3 cell area around (cx, cy).
	Cell area(int cx, int cy) const
	{
		Cell sum;
		for (int y = std::max(cy - 1, 0); y <= std::min(cy + 1, cellsY - 1); ++y)
		{
			for (int x = std::max(cx - 1, 0); x <= std::min(cx + 1, cellsX - 1); ++x)
			{
				const Cell &cell = cells[y * cellsX + x];
				sum.score += cell.score;
				sum.sumX += cell.sumX;
				sum.sumY += cell.sumY;
			}
		}
		return sum;
	}
};

unsigned nextScanTime = 0;
unsigned lastMoveTime = 0;
unsigned lastGameTime = 0;
bool hasTarget = false;
Vector2i target(0, 0);

bool droidAttacks(const DROID &droid)
{
	switch (droid.action)
	{
	case DACTION_ATTACK:
	case DACTION_MOVEFIRE:
	case DACTION_MOVETOATTACK:
	case DACTION_ROTATETOATTACK:
	case DACTION_VTOLATTACK:
		return true;
	default:
		return false;
	}
}

bool hitRecently(const BASE_OBJECT &object)
{
	return object.timeLastHit != 0 && gameTime - object.timeLastHit < RECENT_HIT_MS;
}

/// Spectators and god mode see the whole map. Other players see only their own vision,
/// and the camera must not move into the fog of war.
bool localPlayerSeesAll()
{
	if (godMode)
	{
		return true;
	}
	return selectedPlayer < MAX_PLAYERS && NetPlay.players[selectedPlayer].isSpectator;
}

bool objectIsShown(const BASE_OBJECT &object, bool seesAll)
{
	return seesAll || object.visibleForLocalDisplay() != 0;
}

void scoreObjects(ScoreGrid &grid)
{
	const bool seesAll = localPlayerSeesAll();
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		for (const DROID *psDroid : gameWorld.objects.droids[player])
		{
			if (isDead(psDroid) || !objectIsShown(*psDroid, seesAll))
			{
				continue;
			}
			int score = 0;
			if (droidAttacks(*psDroid))
			{
				score += SCORE_ATTACK;
			}
			if (hitRecently(*psDroid))
			{
				score += SCORE_HIT;
			}
			if (psDroid->sMove.Status != MOVEINACTIVE)
			{
				score += SCORE_MOVE;
			}
			if (score > 0)
			{
				grid.add(psDroid->pos, score);
			}
		}
		for (const STRUCTURE *psStruct : gameWorld.objects.structures[player])
		{
			if (isDead(psStruct) || !objectIsShown(*psStruct, seesAll))
			{
				continue;
			}
			if (hitRecently(*psStruct))
			{
				grid.add(psStruct->pos, SCORE_STRUCTURE_HIT);
			}
		}
	}
}

} // anonymous namespace

void autoCameraUpdate()
{
	if (!autocamera_enabled())
	{
		return;
	}
	// A new game starts the game clock again. Then the old target is not valid.
	if (gameTime < lastGameTime)
	{
		hasTarget = false;
		lastMoveTime = 0;
		nextScanTime = 0;
	}
	lastGameTime = gameTime;

	if (realTime < nextScanTime)
	{
		return;
	}
	nextScanTime = realTime + SCAN_INTERVAL_MS;

	// Do not interrupt a camera slide or a droid that the user tracks.
	if (getWarCamStatus())
	{
		return;
	}
	if (gameWorld.map.width <= 0 || gameWorld.map.height <= 0)
	{
		return;
	}

	ScoreGrid grid;
	grid.cellsX = (gameWorld.map.width + CELL_TILES - 1) / CELL_TILES;
	grid.cellsY = (gameWorld.map.height + CELL_TILES - 1) / CELL_TILES;
	grid.cells.assign(static_cast<size_t>(grid.cellsX) * grid.cellsY, Cell());
	scoreObjects(grid);

	Cell best;
	for (int cy = 0; cy < grid.cellsY; ++cy)
	{
		for (int cx = 0; cx < grid.cellsX; ++cx)
		{
			const Cell area = grid.area(cx, cy);
			if (area.score > best.score)
			{
				best = area;
			}
		}
	}
	if (best.score <= 0)
	{
		return;
	}

	if (hasTarget)
	{
		if (realTime - lastMoveTime < HOLD_MS)
		{
			return;
		}
		const int targetX = std::min(target.x / CELL_UNITS, grid.cellsX - 1);
		const int targetY = std::min(target.y / CELL_UNITS, grid.cellsY - 1);
		const Cell current = grid.area(targetX, targetY);
		if (current.score * 100 >= best.score * STAY_PERCENT)
		{
			return;
		}
	}

	const Vector2i newTarget(static_cast<int>(best.sumX / best.score), static_cast<int>(best.sumY / best.score));
	const Vector2i cameraPos(playerPos.p.x, playerPos.p.z);
	hasTarget = true;
	target = newTarget;
	lastMoveTime = realTime;
	if (glm::length(Vector2f(newTarget - cameraPos)) < MIN_MOVE_UNITS)
	{
		return;
	}
	debug(LOG_3D, "autocamera: move to tile (%d, %d), score %d", map_coord(newTarget.x), map_coord(newTarget.y), best.score);
	requestRadarTrack(newTarget.x, newTarget.y);
}
