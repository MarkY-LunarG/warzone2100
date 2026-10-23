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
/** @file
 *  Debug aid: plays a script of gamepad input on an SDL virtual gamepad (see sdl_gamepad_script.h).
 */

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/framework/input.h"
#include "lib/ivis_opengl/screen.h"
#include "sdl_gamepad_script.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// The frames a press holds a button, and the frames after its release
const int PRESS_FRAMES = 4;
// A flick holds the stick fully over for this many frames: well inside the flick time limit
const int FLICK_FRAMES = 3;

enum class Action
{
	Wait,
	WaitMs,
	Press,
	Flick,
	Stick,
	MoveTo,
	Screenshot,
	Log,
	Fail,
	Quit
};

struct Command
{
	Action action = Action::Wait;
	int frames = 0;            // wait, press, stick
	uint32_t milliseconds = 0; // waitms
	SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
	SDL_GamepadAxis axisX = SDL_GAMEPAD_AXIS_LEFTX;
	SDL_GamepadAxis axisY = SDL_GAMEPAD_AXIS_LEFTY;
	float x = 0.f;
	float y = 0.f;
	std::string text;          // screenshot label, log text
	int line = 0;
};

const std::map<std::string, SDL_GamepadButton> buttonNames = {
	{"a", SDL_GAMEPAD_BUTTON_SOUTH}, {"b", SDL_GAMEPAD_BUTTON_EAST}, {"x", SDL_GAMEPAD_BUTTON_WEST}, {"y", SDL_GAMEPAD_BUTTON_NORTH},
	{"back", SDL_GAMEPAD_BUTTON_BACK}, {"guide", SDL_GAMEPAD_BUTTON_GUIDE}, {"start", SDL_GAMEPAD_BUTTON_START},
	{"leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
	{"leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
	{"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
	{"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

std::vector<Command> commands;
size_t commandIndex = 0;
int commandFrame = 0;        // frames spent in the current command
uint32_t commandStartTicks = 0;
SDL_JoystickID virtualId = 0;
SDL_Joystick* virtualJoystick = nullptr;
bool failed = false;

bool parseScript(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		debug(LOG_ERROR, "Gamepad script: could not open %s", path);
		return false;
	}
	std::string line;
	int lineNumber = 0;
	while (std::getline(file, line))
	{
		++lineNumber;
		const size_t comment = line.find('#');
		if (comment != std::string::npos)
		{
			line.erase(comment);
		}
		std::istringstream words(line);
		std::string verb;
		if (!(words >> verb))
		{
			continue;
		}
		Command command;
		command.line = lineNumber;
		bool valid = true;
		if (verb == "wait")
		{
			command.action = Action::Wait;
			valid = static_cast<bool>(words >> command.frames);
		}
		else if (verb == "waitms")
		{
			command.action = Action::WaitMs;
			valid = static_cast<bool>(words >> command.milliseconds);
		}
		else if (verb == "press")
		{
			std::string name;
			command.action = Action::Press;
			command.frames = PRESS_FRAMES;
			valid = static_cast<bool>(words >> name) && buttonNames.count(name) != 0;
			if (valid)
			{
				command.button = buttonNames.at(name);
				words >> command.frames;
			}
		}
		else if (verb == "flick")
		{
			std::string direction;
			command.action = Action::Flick;
			command.frames = FLICK_FRAMES;
			valid = static_cast<bool>(words >> direction);
			command.x = (direction == "left") ? -1.f : ((direction == "right") ? 1.f : 0.f);
			command.y = (direction == "up") ? -1.f : ((direction == "down") ? 1.f : 0.f);
			valid = valid && (command.x != 0.f || command.y != 0.f);
		}
		else if (verb == "stick")
		{
			std::string which;
			command.action = Action::Stick;
			valid = static_cast<bool>(words >> which >> command.x >> command.y >> command.frames) && (which == "left" || which == "right");
			command.axisX = (which == "right") ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
			command.axisY = (which == "right") ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
		}
		else if (verb == "moveto")
		{
			command.action = Action::MoveTo;
			valid = static_cast<bool>(words >> command.x >> command.y);
		}
		else if (verb == "screenshot" || verb == "log" || verb == "fail")
		{
			command.action = (verb == "screenshot") ? Action::Screenshot : (verb == "log") ? Action::Log : Action::Fail;
			std::getline(words >> std::ws, command.text);
			valid = !command.text.empty();
		}
		else if (verb == "quit")
		{
			command.action = Action::Quit;
		}
		else
		{
			valid = false;
		}
		if (!valid)
		{
			debug(LOG_ERROR, "Gamepad script %s, line %d: cannot read \"%s\"", path, lineNumber, line.c_str());
			return false;
		}
		commands.push_back(command);
	}
	return true;
}

Sint16 axisValue(float value)
{
	return static_cast<Sint16>(std::clamp(value, -1.f, 1.f) * 32767.f);
}

// Runs the current command for one frame. Returns true when it is done.
bool stepCommand(const Command& command)
{
	switch (command.action)
	{
		case Action::Wait:
			return commandFrame >= command.frames;
		case Action::WaitMs:
			return SDL_GetTicks() - commandStartTicks >= command.milliseconds;
		case Action::Press:
			SDL_SetJoystickVirtualButton(virtualJoystick, command.button, commandFrame < command.frames);
			return commandFrame >= command.frames + PRESS_FRAMES;
		case Action::Flick:
		case Action::Stick:
		{
			const bool held = commandFrame < command.frames;
			SDL_SetJoystickVirtualAxis(virtualJoystick, command.axisX, held ? axisValue(command.x) : 0);
			SDL_SetJoystickVirtualAxis(virtualJoystick, command.axisY, held ? axisValue(command.y) : 0);
			// after a flick, let the cursor finish its hop
			return commandFrame >= command.frames + ((command.action == Action::Flick) ? 2 * PRESS_FRAMES : 1);
		}
		case Action::MoveTo:
		{
			// Deflect the stick in proportion to the distance left (full deflection from 200 pixels),
			// as a player would, with enough deflection to keep moving when close.
			const float dx = command.x - static_cast<float>(mouseX());
			const float dy = command.y - static_cast<float>(mouseY());
			const float distance = std::sqrt(dx * dx + dy * dy);
			const bool arrived = distance <= 3.f;
			const bool timedOut = SDL_GetTicks() - commandStartTicks > 10000;
			if (arrived || timedOut)
			{
				SDL_SetJoystickVirtualAxis(virtualJoystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
				SDL_SetJoystickVirtualAxis(virtualJoystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
				if (timedOut)
				{
					debug(LOG_ERROR, "Gamepad script, line %d: the cursor did not reach %g, %g (it is at %d, %d)", command.line, command.x, command.y, mouseX(), mouseY());
					failed = true;
				}
				return true;
			}
			const float deflection = std::clamp(distance / 200.f, 0.3f, 1.f);
			SDL_SetJoystickVirtualAxis(virtualJoystick, SDL_GAMEPAD_AXIS_LEFTX, axisValue(dx / distance * deflection));
			SDL_SetJoystickVirtualAxis(virtualJoystick, SDL_GAMEPAD_AXIS_LEFTY, axisValue(dy / distance * deflection));
			return false;
		}
		case Action::Screenshot:
			screenDumpToDisk("screenshots/", command.text.c_str());
			debug(LOG_INFO, "Gamepad script, line %d: screenshot %s", command.line, command.text.c_str());
			return true;
		case Action::Fail:
			debug(LOG_ERROR, "Gamepad script, line %d: %s", command.line, command.text.c_str());
			failed = true;
			return true;
		case Action::Log:
			debug(LOG_INFO, "Gamepad script, line %d: %s", command.line, command.text.c_str());
			return true;
		case Action::Quit:
			debug(LOG_INFO, "Gamepad script, line %d: quit", command.line);
			wzQuit(0);
			return true;
	}
	return true;
}

} // anonymous namespace

void wzGamepadScriptInit()
{
	const char* path = getenv("WZ_DEBUG_GAMEPAD_SCRIPT");
	if (path == nullptr || path[0] == '\0' || virtualJoystick != nullptr)
	{
		return;
	}
	if (!parseScript(path))
	{
		commands.clear();
		return;
	}

	// Every SDL gamepad button and axis, in SDL's order, so that SDL maps button n to SDL_GamepadButton n
	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
	desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
	desc.name = "Warzone 2100 script gamepad";
	virtualId = SDL_AttachVirtualJoystick(&desc);
	if (virtualId == 0)
	{
		debug(LOG_ERROR, "Gamepad script: could not attach a virtual gamepad: %s", SDL_GetError());
		commands.clear();
		return;
	}
	virtualJoystick = SDL_OpenJoystick(virtualId);
	if (virtualJoystick == nullptr)
	{
		debug(LOG_ERROR, "Gamepad script: could not open the virtual gamepad: %s", SDL_GetError());
		SDL_DetachVirtualJoystick(virtualId);
		virtualId = 0;
		commands.clear();
		return;
	}
	commandIndex = 0;
	commandFrame = 0;
	commandStartTicks = SDL_GetTicks();
	debug(LOG_INFO, "Gamepad script: %s, %zu commands, on a virtual gamepad", path, commands.size());
}

void wzGamepadScriptUpdate()
{
	if (virtualJoystick == nullptr || commandIndex >= commands.size())
	{
		return;
	}
	if (commandFrame == 0)
	{
		commandStartTicks = SDL_GetTicks();
	}
	const bool done = stepCommand(commands[commandIndex]);
	++commandFrame;
	if (failed)
	{
		// The rest of the script would act on the wrong widgets: stop, and quit with an error
		debug(LOG_ERROR, "Gamepad script: stopped");
		commandIndex = commands.size();
		wzQuit(1);
		return;
	}
	if (done)
	{
		++commandIndex;
		commandFrame = 0;
		if (commandIndex >= commands.size())
		{
			debug(LOG_INFO, "Gamepad script: done");
		}
	}
}

void wzGamepadScriptShutdown()
{
	if (virtualJoystick != nullptr)
	{
		SDL_CloseJoystick(virtualJoystick);
		virtualJoystick = nullptr;
	}
	if (virtualId != 0)
	{
		SDL_DetachVirtualJoystick(virtualId);
		virtualId = 0;
	}
	commands.clear();
	commandIndex = 0;
}
