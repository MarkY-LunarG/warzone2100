/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

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

// Build tool: prepare a Vulkan SPIR-V module for translation to WGSL by Tint.
//
// Usage: wz_webgpu_spirv_prep <input.spv> <output.spv>
//
// 1. Split each combined image sampler into a separate image and sampler, because
//    WGSL has no combined image samplers.
// 2. Give each resource a fixed binding number. After the split, an image and its
//    sampler have the same binding b. This tool sets the image to 2b, the sampler
//    to 2b+1, and every other resource to 2b. Thus, the bind group layout depends
//    only on the GLSL source, and the WebGPU backend uses the same rule.
//
// Without step 2, Tint renumbers the bindings itself. Its numbers change with the
// resources that each shader uses, so the vertex and fragment shaders of one
// pipeline can disagree.

#include <spirv-tools/optimizer.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace
{

// SPIR-V opcodes and decorations (SPIR-V specification, section 3)
constexpr uint32_t SpirvMagicNumber = 0x07230203;
constexpr uint32_t SpirvHeaderWords = 5;
constexpr uint32_t OpTypeImage = 25;
constexpr uint32_t OpTypeSampler = 26;
constexpr uint32_t OpTypeSampledImage = 27;
constexpr uint32_t OpTypeArray = 28;
constexpr uint32_t OpTypeRuntimeArray = 29;
constexpr uint32_t OpTypePointer = 32;
constexpr uint32_t OpVariable = 59;
constexpr uint32_t OpDecorate = 71;
constexpr uint32_t DecorationBinding = 33;

enum class ResourceKind
{
	Image,
	Sampler,
	CombinedImageSampler,
	Other
};

bool readModule(const char* path, std::vector<uint32_t>& words)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		fprintf(stderr, "error: cannot open %s\n", path);
		return false;
	}
	std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (bytes.size() % sizeof(uint32_t) != 0 || bytes.size() < SpirvHeaderWords * sizeof(uint32_t))
	{
		fprintf(stderr, "error: %s is not a SPIR-V module\n", path);
		return false;
	}
	words.resize(bytes.size() / sizeof(uint32_t));
	std::memcpy(words.data(), bytes.data(), bytes.size());
	if (words[0] != SpirvMagicNumber)
	{
		fprintf(stderr, "error: %s is not a SPIR-V module\n", path);
		return false;
	}
	return true;
}

bool writeModule(const char* path, const std::vector<uint32_t>& words)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		fprintf(stderr, "error: cannot write %s\n", path);
		return false;
	}
	file.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
	return static_cast<bool>(file);
}

bool splitCombinedImageSamplers(const std::vector<uint32_t>& input, std::vector<uint32_t>& output)
{
	spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_0);
	optimizer.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t& position, const char* message) {
		fprintf(stderr, "spirv-opt: %zu: %s\n", position.index, message);
	});
	optimizer.RegisterPass(spvtools::CreateSplitCombinedImageSamplerPass());
	return optimizer.Run(input.data(), input.size(), &output);
}

bool setFixedBindings(std::vector<uint32_t>& words)
{
	std::unordered_map<uint32_t, ResourceKind> typeKinds;     // type id -> kind
	std::unordered_map<uint32_t, uint32_t> arrayElementTypes; // array type id -> element type id
	std::unordered_map<uint32_t, uint32_t> pointeeTypes;      // pointer type id -> pointee type id
	std::unordered_map<uint32_t, uint32_t> variableTypes;     // variable id -> pointer type id
	std::unordered_map<uint32_t, size_t> bindingWords;        // variable id -> index of its Binding value

	for (size_t i = SpirvHeaderWords; i < words.size();)
	{
		const uint32_t wordCount = words[i] >> 16;
		const uint32_t opcode = words[i] & 0xFFFF;
		if (wordCount == 0 || i + wordCount > words.size())
		{
			fprintf(stderr, "error: malformed SPIR-V instruction at word %zu\n", i);
			return false;
		}
		switch (opcode)
		{
			case OpTypeImage:
				typeKinds[words[i + 1]] = ResourceKind::Image;
				break;
			case OpTypeSampler:
				typeKinds[words[i + 1]] = ResourceKind::Sampler;
				break;
			case OpTypeSampledImage:
				typeKinds[words[i + 1]] = ResourceKind::CombinedImageSampler;
				break;
			case OpTypeArray:
			case OpTypeRuntimeArray:
				arrayElementTypes[words[i + 1]] = words[i + 2];
				break;
			case OpTypePointer:
				pointeeTypes[words[i + 1]] = words[i + 3];
				break;
			case OpVariable:
				variableTypes[words[i + 2]] = words[i + 1];
				break;
			case OpDecorate:
				if (wordCount >= 4 && words[i + 2] == DecorationBinding)
				{
					bindingWords[words[i + 1]] = i + 3;
				}
				break;
			default:
				break;
		}
		i += wordCount;
	}

	auto kindOf = [&](uint32_t typeId) {
		auto element = arrayElementTypes.find(typeId);
		while (element != arrayElementTypes.end())
		{
			typeId = element->second;
			element = arrayElementTypes.find(typeId);
		}
		auto kind = typeKinds.find(typeId);
		return (kind != typeKinds.end()) ? kind->second : ResourceKind::Other;
	};

	for (const auto& [variableId, bindingWord] : bindingWords)
	{
		auto pointerType = variableTypes.find(variableId);
		if (pointerType == variableTypes.end())
		{
			continue; // the decoration is not on a variable
		}
		const ResourceKind kind = kindOf(pointeeTypes[pointerType->second]);
		if (kind == ResourceKind::CombinedImageSampler)
		{
			fprintf(stderr, "error: a combined image sampler is left after the split\n");
			return false;
		}
		const uint32_t binding = words[bindingWord];
		words[bindingWord] = (kind == ResourceKind::Sampler) ? (2 * binding + 1) : (2 * binding);
	}
	return true;
}

} // anonymous namespace

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <input.spv> <output.spv>\n", argv[0]);
		return 2;
	}

	std::vector<uint32_t> input;
	if (!readModule(argv[1], input))
	{
		return 1;
	}
	std::vector<uint32_t> output;
	if (!splitCombinedImageSamplers(input, output))
	{
		fprintf(stderr, "error: the split pass failed for %s\n", argv[1]);
		return 1;
	}
	if (!setFixedBindings(output))
	{
		fprintf(stderr, "error: failed to set the binding numbers for %s\n", argv[1]);
		return 1;
	}
	return writeModule(argv[2], output) ? 0 : 1;
}
