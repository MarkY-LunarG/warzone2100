/*
* Deterministic pipeline identifiers
*
* Vulkan SC matches a pipeline to its offline-compiled entry by an application-chosen 16-byte
* identifier: VkPipelineOfflineCreateInfo::pipelineIdentifier at run time has to equal the
* "PipelineUUID" in the pipeline JSON the pipeline cache compiler consumed.
*
* Keeping those two in sync by hand is the most fragile part of a Vulkan SC application -- it is
* two constants in two files that must never drift. Instead both sides derive the identifier from
* the same string, "<sample>/<pipeline>", with the same hash. scripts/pipeline_uuid.py implements
* the identical function for authoring and checking the JSON.
*
* The hash is FNV-1a, run twice with different offset bases to fill 16 bytes. It is not a
* cryptographic hash and does not need to be: it only has to be stable and collision-free across
* the handful of pipeline names in one application.
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <cstdint>
#include <string>

namespace vks
{

inline uint64_t fnv1a64(const std::string& text, uint64_t offsetBasis)
{
	constexpr uint64_t prime = 1099511628211ULL;
	uint64_t hash = offsetBasis;
	for (unsigned char c : text)
	{
		hash ^= static_cast<uint64_t>(c);
		hash *= prime;
	}
	return hash;
}

// Writes the 16-byte identifier for a pipeline name into out.
inline void computePipelineIdentifier(const std::string& pipelineName, uint8_t out[16])
{
	constexpr uint64_t basisA = 14695981039346656037ULL; // the standard FNV-1a offset basis
	constexpr uint64_t basisB = 14695981039346656037ULL ^ 0x5bf03635ULL;

	const uint64_t high = fnv1a64(pipelineName, basisA);
	const uint64_t low = fnv1a64(pipelineName, basisB);

	for (int i = 0; i < 8; i++)
	{
		out[i] = static_cast<uint8_t>((high >> (8 * (7 - i))) & 0xFF);
		out[8 + i] = static_cast<uint8_t>((low >> (8 * (7 - i))) & 0xFF);
	}
}

} // namespace vks
