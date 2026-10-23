/*
* Pipeline JSON export
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanSCPipelineExport.h"
#include "VulkanSCEnumStrings.h"
#include "VulkanSCPipelineIdentifier.h"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace vks
{
namespace
{

using namespace enumstrings;

// A small JSON writer. The schema is fixed and shallow, so a stringstream with explicit
// indentation is clearer here than pulling in a JSON library, and it keeps the output ordered the
// way the reference documents are.
class JsonWriter
{
  public:
	explicit JsonWriter(int indent) : indentLevel(indent) {}

	void beginObject() { out << "{\n"; indentLevel++; }
	void endObject() { trimComma(); indentLevel--; pad(); out << "}"; }
	void beginArray() { out << "[\n"; indentLevel++; }
	void endArray() { trimComma(); indentLevel--; pad(); out << "]"; }

	void key(const std::string& k) { pad(); out << "\"" << k << "\": "; }
	void comma() { out << ",\n"; }

	// A quoted value. Enum names and the "NULL" sentinel both go through here.
	void quoted(const std::string& k, const std::string& v)
	{
		key(k);
		out << "\"" << v << "\"";
		comma();
	}

	void number(const std::string& k, int64_t v)
	{
		key(k);
		out << v;
		comma();
	}

	void real(const std::string& k, double v)
	{
		key(k);
		// Whole numbers are written without a decimal point, matching the reference documents.
		if (v == static_cast<int64_t>(v))
		{
			out << static_cast<int64_t>(v);
		}
		else
		{
			out << v;
		}
		comma();
	}

	void pad()
	{
		for (int i = 0; i < indentLevel; i++)
		{
			out << "    ";
		}
	}

	// Removes the trailing ",\n" a value emitter leaves behind, so the last member of an object or
	// array does not carry a comma.
	void trimComma()
	{
		std::string s = out.str();
		if (s.size() >= 2 && s.compare(s.size() - 2, 2, ",\n") == 0)
		{
			s.erase(s.size() - 2);
			s += "\n";
			out.str(s);
			out.seekp(0, std::ios_base::end);
		}
	}

	std::string str() const { return out.str(); }
	std::ostringstream& stream() { return out; }
	int indent() const { return indentLevel; }

  private:
	std::ostringstream out;
	int indentLevel;
};

const char* boolString(VkBool32 value)
{
	return value ? "VK_TRUE" : "VK_FALSE";
}

// Specialization data is carried as base64 in the pipeline JSON.
std::string base64(const uint8_t* data, size_t size)
{
	static const char* table =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve(((size + 2) / 3) * 4);
	for (size_t i = 0; i < size; i += 3)
	{
		const uint32_t remaining = static_cast<uint32_t>(size - i);
		const uint32_t block = (static_cast<uint32_t>(data[i]) << 16) |
		                       (remaining > 1 ? static_cast<uint32_t>(data[i + 1]) << 8 : 0u) |
		                       (remaining > 2 ? static_cast<uint32_t>(data[i + 2]) : 0u);
		out += table[(block >> 18) & 0x3F];
		out += table[(block >> 12) & 0x3F];
		out += remaining > 1 ? table[(block >> 6) & 0x3F] : '=';
		out += remaining > 2 ? table[block & 0x3F] : '=';
	}
	return out;
}

void writePNext(JsonWriter& w, const void* pNext, std::set<std::string>& unsupported);

// One shader stage, shared by the graphics and compute paths. The module is always 0: Vulkan SC
// has no shader modules, and the compiler takes the code from the file named in ShaderFileNames.
void writeShaderStage(JsonWriter& w,
                      const VkPipelineShaderStageCreateInfo& stage,
                      std::set<std::string>& unsupported)
{
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO");
	writePNext(w, stage.pNext, unsupported);
	w.quoted("flags", "0");
	w.quoted("stage", shaderStageFlagsToString(stage.stage));
	w.number("module", 0);
	w.quoted("pName", "main");

	if (stage.pSpecializationInfo == nullptr || stage.pSpecializationInfo->mapEntryCount == 0)
	{
		w.quoted("pSpecializationInfo", "NULL");
	}
	else
	{
		const VkSpecializationInfo& spec = *stage.pSpecializationInfo;
		w.key("pSpecializationInfo");
		w.beginObject();
		w.number("mapEntryCount", spec.mapEntryCount);
		w.key("pMapEntries");
		w.beginArray();
		for (uint32_t i = 0; i < spec.mapEntryCount; i++)
		{
			w.pad();
			w.beginObject();
			w.number("constantID", spec.pMapEntries[i].constantID);
			w.number("offset", spec.pMapEntries[i].offset);
			w.number("size", static_cast<int64_t>(spec.pMapEntries[i].size));
			w.endObject();
			w.comma();
		}
		w.endArray();
		w.comma();
		w.number("dataSize", static_cast<int64_t>(spec.dataSize));
		w.quoted("pData",
		         base64(static_cast<const uint8_t*>(spec.pData), spec.dataSize));
		w.endObject();
		w.comma();
	}
	w.endObject();
}

void writeStencilOpState(JsonWriter& w, const std::string& name, const VkStencilOpState& state)
{
	w.key(name);
	w.beginObject();
	w.quoted("failOp", toString(state.failOp));
	w.quoted("passOp", toString(state.passOp));
	w.quoted("depthFailOp", toString(state.depthFailOp));
	w.quoted("compareOp", toString(state.compareOp));
	w.number("compareMask", state.compareMask);
	w.number("writeMask", state.writeMask);
	w.number("reference", state.reference);
	w.endObject();
	w.comma();
}

// An array of numbers on one line, as blendConstants is written.
template <typename T>
void writeNumberArray(JsonWriter& w, const std::string& name, const T* values, uint32_t count)
{
	w.key(name);
	w.stream() << "[";
	for (uint32_t i = 0; i < count; i++)
	{
		w.stream() << (i ? ", " : "") << static_cast<int64_t>(values[i]);
	}
	w.stream() << "]";
	w.comma();
}

// One attachment reference, in the second structure's form. aspectMask is only meaningful for an
// input attachment; it is written for every reference because the structure has it.
void writeAttachmentReference2(JsonWriter& w,
                               const VkAttachmentReference2& r,
                               std::set<std::string>& unsupported)
{
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2");
	writePNext(w, r.pNext, unsupported);
	w.number("attachment", r.attachment);
	w.quoted("layout", toString(r.layout));
	w.number("aspectMask", r.aspectMask);
	w.endObject();
}

// Writes a structure's pNext member: "NULL", or the chained structure as a nested object with its
// own pNext, which is how the Khronos pipeline JSON parser reads a chain. Only the structures the
// samples chain are known. Any other is added to unsupported, and the export then fails, because
// leaving it out would describe a pipeline the code does not build.
void writePNext(JsonWriter& w, const void* pNext, std::set<std::string>& unsupported)
{
	const auto* base = static_cast<const VkBaseInStructure*>(pNext);
	if (base == nullptr)
	{
		w.quoted("pNext", "NULL");
		return;
	}

	w.key("pNext");
	w.beginObject();
	w.quoted("sType", toString(base->sType));
	writePNext(w, base->pNext, unsupported);
	switch (base->sType)
	{
	case VK_STRUCTURE_TYPE_PIPELINE_OFFLINE_CREATE_INFO:
	{
		const auto& s = *reinterpret_cast<const VkPipelineOfflineCreateInfo*>(base);
		writeNumberArray(w, "pipelineIdentifier", s.pipelineIdentifier, VK_UUID_SIZE);
		w.quoted("matchControl", toString(s.matchControl));
		// A string, as the Khronos documents write it: the parser reads a VkDeviceSize as one.
		w.quoted("poolEntrySize", std::to_string(s.poolEntrySize));
		break;
	}
	case VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT:
	{
		const auto& s =
			*reinterpret_cast<const VkPipelineRasterizationConservativeStateCreateInfoEXT*>(base);
		w.number("flags", 0);
		w.quoted("conservativeRasterizationMode", toString(s.conservativeRasterizationMode));
		w.real("extraPrimitiveOverestimationSize", s.extraPrimitiveOverestimationSize);
		break;
	}
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO:
	{
		const auto& s = *reinterpret_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(base);
		w.number("bindingCount", s.bindingCount);
		w.key("pBindingFlags");
		w.beginArray();
		for (uint32_t i = 0; i < s.bindingCount; i++)
		{
			w.pad();
			w.stream() << "\"" << descriptorBindingFlagsToString(s.pBindingFlags[i]) << "\"";
			w.comma();
		}
		w.endArray();
		w.comma();
		break;
	}
	case VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO:
	{
		const auto& s = *reinterpret_cast<const VkRenderPassMultiviewCreateInfo*>(base);
		w.number("subpassCount", s.subpassCount);
		writeNumberArray(w, "pViewMasks", s.pViewMasks, s.subpassCount);
		w.number("dependencyCount", s.dependencyCount);
		writeNumberArray(w, "pViewOffsets", s.pViewOffsets, s.dependencyCount);
		w.number("correlationMaskCount", s.correlationMaskCount);
		writeNumberArray(w, "pCorrelationMasks", s.pCorrelationMasks, s.correlationMaskCount);
		break;
	}
	case VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR:
	{
		const auto& s = *reinterpret_cast<const VkFragmentShadingRateAttachmentInfoKHR*>(base);
		if (s.pFragmentShadingRateAttachment != nullptr)
		{
			w.key("pFragmentShadingRateAttachment");
			writeAttachmentReference2(w, *s.pFragmentShadingRateAttachment, unsupported);
			w.comma();
		}
		else
		{
			w.quoted("pFragmentShadingRateAttachment", "NULL");
		}
		w.key("shadingRateAttachmentTexelSize");
		w.beginObject();
		w.number("width", s.shadingRateAttachmentTexelSize.width);
		w.number("height", s.shadingRateAttachmentTexelSize.height);
		w.endObject();
		w.comma();
		break;
	}
	default:
		unsupported.insert(toString(base->sType));
		break;
	}
	w.endObject();
	w.comma();
}

} // namespace

void PipelineExporter::recordRenderPass(const VkRenderPassCreateInfo& info,
                                        VkRenderPass handle)
{
	JsonWriter w(3);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO");
	writePNext(w, info.pNext, unsupportedStructures);
	w.quoted("flags", "0");
	w.number("attachmentCount", info.attachmentCount);

	w.key("pAttachments");
	w.beginArray();
	for (uint32_t i = 0; i < info.attachmentCount; i++)
	{
		const VkAttachmentDescription& a = info.pAttachments[i];
		w.pad();
		w.beginObject();
		w.quoted("flags", "0");
		w.quoted("format", toString(a.format));
		w.quoted("samples", sampleCountFlagsToString(a.samples));
		w.quoted("loadOp", toString(a.loadOp));
		w.quoted("storeOp", toString(a.storeOp));
		w.quoted("stencilLoadOp", toString(a.stencilLoadOp));
		w.quoted("stencilStoreOp", toString(a.stencilStoreOp));
		w.quoted("initialLayout", toString(a.initialLayout));
		w.quoted("finalLayout", toString(a.finalLayout));
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.number("subpassCount", info.subpassCount);
	w.key("pSubpasses");
	w.beginArray();
	for (uint32_t i = 0; i < info.subpassCount; i++)
	{
		const VkSubpassDescription& s = info.pSubpasses[i];
		w.pad();
		w.beginObject();
		w.quoted("flags", "0");
		w.quoted("pipelineBindPoint", toString(s.pipelineBindPoint));
		w.number("inputAttachmentCount", s.inputAttachmentCount);
		if (s.inputAttachmentCount == 0)
		{
			w.quoted("pInputAttachments", "NULL");
		}
		else
		{
			w.key("pInputAttachments");
			w.beginArray();
			for (uint32_t j = 0; j < s.inputAttachmentCount; j++)
			{
				w.pad();
				w.beginObject();
				w.number("attachment", s.pInputAttachments[j].attachment);
				w.quoted("layout", toString(s.pInputAttachments[j].layout));
				w.endObject();
				w.comma();
			}
			w.endArray();
			w.comma();
		}

		w.number("colorAttachmentCount", s.colorAttachmentCount);
		w.key("pColorAttachments");
		w.beginArray();
		for (uint32_t j = 0; j < s.colorAttachmentCount; j++)
		{
			w.pad();
			w.beginObject();
			w.number("attachment", s.pColorAttachments[j].attachment);
			w.quoted("layout", toString(s.pColorAttachments[j].layout));
			w.endObject();
			w.comma();
		}
		w.endArray();
		w.comma();

		// Warzone 2100: written out (was always "NULL"). An array of references, one per color attachment.
		if (s.pResolveAttachments != nullptr)
		{
			w.key("pResolveAttachments");
			w.beginArray();
			for (uint32_t j = 0; j < s.colorAttachmentCount; j++)
			{
				w.pad();
				w.beginObject();
				w.number("attachment", s.pResolveAttachments[j].attachment);
				w.quoted("layout", toString(s.pResolveAttachments[j].layout));
				w.endObject();
				w.comma();
			}
			w.endArray();
			w.comma();
		}
		else
		{
			w.quoted("pResolveAttachments", "NULL");
		}

		if (s.pDepthStencilAttachment != nullptr)
		{
			w.key("pDepthStencilAttachment");
			w.beginObject();
			w.number("attachment", s.pDepthStencilAttachment->attachment);
			w.quoted("layout", toString(s.pDepthStencilAttachment->layout));
			w.endObject();
			w.comma();
		}
		else
		{
			w.quoted("pDepthStencilAttachment", "NULL");
		}

		w.number("preserveAttachmentCount", s.preserveAttachmentCount);
		w.key("pPreserveAttachments");
		w.beginArray();
		w.endArray();
		w.comma();

		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.number("dependencyCount", info.dependencyCount);
	w.key("pDependencies");
	w.beginArray();
	for (uint32_t i = 0; i < info.dependencyCount; i++)
	{
		const VkSubpassDependency& d = info.pDependencies[i];
		w.pad();
		w.beginObject();
		if (d.srcSubpass == VK_SUBPASS_EXTERNAL)
		{
			w.quoted("srcSubpass", "VK_SUBPASS_EXTERNAL");
		}
		else
		{
			w.number("srcSubpass", d.srcSubpass);
		}
		if (d.dstSubpass == VK_SUBPASS_EXTERNAL)
		{
			w.quoted("dstSubpass", "VK_SUBPASS_EXTERNAL");
		}
		else
		{
			w.number("dstSubpass", d.dstSubpass);
		}
		w.quoted("srcStageMask", pipelineStageFlagsToString(d.srcStageMask));
		w.quoted("dstStageMask", pipelineStageFlagsToString(d.dstStageMask));
		// A zero access mask is written as the number 0 rather than a name, which is what the
		// reference documents do.
		if (d.srcAccessMask == 0)
		{
			w.number("srcAccessMask", 0);
		}
		else
		{
			w.quoted("srcAccessMask", accessFlagsToString(d.srcAccessMask));
		}
		if (d.dstAccessMask == 0)
		{
			w.number("dstAccessMask", 0);
		}
		else
		{
			w.quoted("dstAccessMask", accessFlagsToString(d.dstAccessMask));
		}
		if (d.dependencyFlags == 0)
		{
			w.number("dependencyFlags", 0);
		}
		else
		{
			w.quoted("dependencyFlags", dependencyFlagsToString(d.dependencyFlags));
		}
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.endObject();
	renderPasses[handle] = w.str();
}

void PipelineExporter::recordRenderPass2(const VkRenderPassCreateInfo2& info,
                                         VkRenderPass handle)
{
	JsonWriter w(3);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2");
	writePNext(w, info.pNext, unsupportedStructures);
	w.quoted("flags", "0");
	w.number("attachmentCount", info.attachmentCount);

	w.key("pAttachments");
	w.beginArray();
	for (uint32_t i = 0; i < info.attachmentCount; i++)
	{
		const VkAttachmentDescription2& a = info.pAttachments[i];
		w.pad();
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2");
		writePNext(w, a.pNext, unsupportedStructures);
		w.quoted("flags", "0");
		w.quoted("format", toString(a.format));
		w.quoted("samples", sampleCountFlagsToString(a.samples));
		w.quoted("loadOp", toString(a.loadOp));
		w.quoted("storeOp", toString(a.storeOp));
		w.quoted("stencilLoadOp", toString(a.stencilLoadOp));
		w.quoted("stencilStoreOp", toString(a.stencilStoreOp));
		w.quoted("initialLayout", toString(a.initialLayout));
		w.quoted("finalLayout", toString(a.finalLayout));
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.number("subpassCount", info.subpassCount);
	w.key("pSubpasses");
	w.beginArray();
	for (uint32_t i = 0; i < info.subpassCount; i++)
	{
		const VkSubpassDescription2& s2 = info.pSubpasses[i];
		w.pad();
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2");

		// The whole reason this form exists here: a fragment shading rate attachment chains onto
		// the subpass, and the first structure has nowhere to put it.
		writePNext(w, s2.pNext, unsupportedStructures);

		w.quoted("flags", "0");
		w.quoted("pipelineBindPoint", toString(s2.pipelineBindPoint));
		w.number("viewMask", s2.viewMask);

		w.number("inputAttachmentCount", s2.inputAttachmentCount);
		if (s2.inputAttachmentCount == 0)
		{
			w.quoted("pInputAttachments", "NULL");
		}
		else
		{
			w.key("pInputAttachments");
			w.beginArray();
			for (uint32_t j = 0; j < s2.inputAttachmentCount; j++)
			{
				w.pad();
				writeAttachmentReference2(w, s2.pInputAttachments[j], unsupportedStructures);
				w.comma();
			}
			w.endArray();
			w.comma();
		}

		w.number("colorAttachmentCount", s2.colorAttachmentCount);
		w.key("pColorAttachments");
		w.beginArray();
		for (uint32_t j = 0; j < s2.colorAttachmentCount; j++)
		{
			w.pad();
			writeAttachmentReference2(w, s2.pColorAttachments[j], unsupportedStructures);
			w.comma();
		}
		w.endArray();
		w.comma();

		// Warzone 2100: written out (was always "NULL").
		if (s2.pResolveAttachments != nullptr)
		{
			w.key("pResolveAttachments");
			w.beginArray();
			for (uint32_t j = 0; j < s2.colorAttachmentCount; j++)
			{
				w.pad();
				writeAttachmentReference2(w, s2.pResolveAttachments[j], unsupportedStructures);
				w.comma();
			}
			w.endArray();
			w.comma();
		}
		else
		{
			w.quoted("pResolveAttachments", "NULL");
		}

		if (s2.pDepthStencilAttachment != nullptr)
		{
			w.key("pDepthStencilAttachment");
			writeAttachmentReference2(w, *s2.pDepthStencilAttachment, unsupportedStructures);
			w.comma();
		}
		else
		{
			w.quoted("pDepthStencilAttachment", "NULL");
		}

		w.number("preserveAttachmentCount", s2.preserveAttachmentCount);
		w.key("pPreserveAttachments");
		w.beginArray();
		w.endArray();
		w.comma();

		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.number("dependencyCount", info.dependencyCount);
	w.key("pDependencies");
	w.beginArray();
	for (uint32_t i = 0; i < info.dependencyCount; i++)
	{
		const VkSubpassDependency2& d = info.pDependencies[i];
		w.pad();
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2");
		writePNext(w, d.pNext, unsupportedStructures);
		if (d.srcSubpass == VK_SUBPASS_EXTERNAL)
		{
			w.quoted("srcSubpass", "VK_SUBPASS_EXTERNAL");
		}
		else
		{
			w.number("srcSubpass", d.srcSubpass);
		}
		if (d.dstSubpass == VK_SUBPASS_EXTERNAL)
		{
			w.quoted("dstSubpass", "VK_SUBPASS_EXTERNAL");
		}
		else
		{
			w.number("dstSubpass", d.dstSubpass);
		}
		w.quoted("srcStageMask", pipelineStageFlagsToString(d.srcStageMask));
		w.quoted("dstStageMask", pipelineStageFlagsToString(d.dstStageMask));
		if (d.srcAccessMask == 0)
		{
			w.number("srcAccessMask", 0);
		}
		else
		{
			w.quoted("srcAccessMask", accessFlagsToString(d.srcAccessMask));
		}
		if (d.dstAccessMask == 0)
		{
			w.number("dstAccessMask", 0);
		}
		else
		{
			w.quoted("dstAccessMask", accessFlagsToString(d.dstAccessMask));
		}
		if (d.dependencyFlags == 0)
		{
			w.number("dependencyFlags", 0);
		}
		else
		{
			w.quoted("dependencyFlags", dependencyFlagsToString(d.dependencyFlags));
		}
		w.number("viewOffset", d.viewOffset);
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();

	w.number("correlatedViewMaskCount", info.correlatedViewMaskCount);
	w.key("pCorrelatedViewMasks");
	w.beginArray();
	w.endArray();
	w.comma();

	w.endObject();
	renderPasses[handle] = w.str();
}

void PipelineExporter::recordDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& info,
                                                 VkDescriptorSetLayout handle)
{
	JsonWriter w(4);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO");
	writePNext(w, info.pNext, unsupportedStructures);
	w.quoted("flags", "0");
	w.number("bindingCount", info.bindingCount);
	w.key("pBindings");
	w.beginArray();
	for (uint32_t i = 0; i < info.bindingCount; i++)
	{
		const VkDescriptorSetLayoutBinding& b = info.pBindings[i];
		w.pad();
		w.beginObject();
		w.number("binding", b.binding);
		w.quoted("descriptorType", toString(b.descriptorType));
		w.number("descriptorCount", b.descriptorCount);
		w.quoted("stageFlags", shaderStageFlagsToString(b.stageFlags));
		w.quoted("pImmutableSamplers", "NULL");
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();
	w.endObject();
	descriptorSetLayouts[handle] = w.str();
}

void PipelineExporter::recordPipelineLayout(const VkPipelineLayoutCreateInfo& info,
                                            VkPipelineLayout handle)
{
	// The set-layout ids are assigned per document at write time, so the array is emitted then;
	// here only the handles are remembered.
	JsonWriter w(3);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO");
	writePNext(w, info.pNext, unsupportedStructures);
	w.number("flags", 0);
	w.number("setLayoutCount", info.setLayoutCount);
	w.key("pSetLayouts");
	w.stream() << "@SETLAYOUTS@";
	w.comma();
	w.number("pushConstantRangeCount", info.pushConstantRangeCount);
	if (info.pushConstantRangeCount == 0)
	{
		w.quoted("pPushConstantRanges", "NULL");
	}
	else
	{
		w.key("pPushConstantRanges");
		w.beginArray();
		for (uint32_t i = 0; i < info.pushConstantRangeCount; i++)
		{
			const VkPushConstantRange& range = info.pPushConstantRanges[i];
			w.pad();
			w.beginObject();
			w.quoted("stageFlags", shaderStageFlagsToString(range.stageFlags));
			w.number("offset", range.offset);
			w.number("size", range.size);
			w.endObject();
			w.comma();
		}
		w.endArray();
		w.comma();
	}
	w.endObject();

	RecordedPipelineLayout recorded;
	recorded.json = w.str();
	for (uint32_t i = 0; i < info.setLayoutCount; i++)
	{
		recorded.setLayouts.push_back(info.pSetLayouts[i]);
	}
	pipelineLayouts[handle] = recorded;
}

void PipelineExporter::recordGraphicsPipeline(const std::string& pipelineName,
                                              const VkGraphicsPipelineCreateInfo& info,
                                              const std::vector<std::string>& shaderFiles)
{
	JsonWriter w(3);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO");
	writePNext(w, info.pNext, unsupportedStructures);
	w.quoted("flags", "0");
	w.number("stageCount", info.stageCount);

	w.key("pStages");
	w.beginArray();
	std::vector<VkShaderStageFlagBits> stages;
	for (uint32_t i = 0; i < info.stageCount; i++)
	{
		stages.push_back(info.pStages[i].stage);
		w.pad();
		writeShaderStage(w, info.pStages[i], unsupportedStructures);
		w.comma();
	}
	w.endArray();
	w.comma();

	// Vertex input
	const VkPipelineVertexInputStateCreateInfo* vi = info.pVertexInputState;
	w.key("pVertexInputState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO");
	writePNext(w, vi ? vi->pNext : nullptr, unsupportedStructures);
	w.number("flags", 0);
	const uint32_t bindingCount = vi ? vi->vertexBindingDescriptionCount : 0;
	const uint32_t attributeCount = vi ? vi->vertexAttributeDescriptionCount : 0;
	w.number("vertexBindingDescriptionCount", bindingCount);
	w.key("pVertexBindingDescriptions");
	w.beginArray();
	for (uint32_t i = 0; i < bindingCount; i++)
	{
		const VkVertexInputBindingDescription& b = vi->pVertexBindingDescriptions[i];
		w.pad();
		w.beginObject();
		w.number("binding", b.binding);
		w.number("stride", b.stride);
		w.quoted("inputRate", toString(b.inputRate));
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();
	w.number("vertexAttributeDescriptionCount", attributeCount);
	w.key("pVertexAttributeDescriptions");
	w.beginArray();
	for (uint32_t i = 0; i < attributeCount; i++)
	{
		const VkVertexInputAttributeDescription& a = vi->pVertexAttributeDescriptions[i];
		w.pad();
		w.beginObject();
		w.number("location", a.location);
		w.number("binding", a.binding);
		w.quoted("format", toString(a.format));
		w.number("offset", a.offset);
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();
	w.endObject();
	w.comma();

	// Input assembly
	w.key("pInputAssemblyState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO");
	writePNext(w, info.pInputAssemblyState->pNext, unsupportedStructures);
	w.number("flags", 0);
	w.quoted("topology", toString(info.pInputAssemblyState->topology));
	w.quoted("primitiveRestartEnable", boolString(info.pInputAssemblyState->primitiveRestartEnable));
	w.endObject();
	w.comma();

	if (info.pTessellationState != nullptr)
	{
		w.key("pTessellationState");
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO");
		writePNext(w, info.pTessellationState->pNext, unsupportedStructures);
		w.number("flags", 0);
		w.number("patchControlPoints", info.pTessellationState->patchControlPoints);
		w.endObject();
		w.comma();
	}
	else
	{
		w.quoted("pTessellationState", "NULL");
	}

	// Viewport. The viewports and scissors themselves are dynamic in every sample so far, so the
	// arrays are written empty and only the counts matter.
	w.key("pViewportState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO");
	writePNext(w, info.pViewportState->pNext, unsupportedStructures);
	w.number("flags", 0);
	w.number("viewportCount", info.pViewportState->viewportCount);
	w.key("pViewports");
	w.beginArray();
	w.endArray();
	w.comma();
	w.number("scissorCount", info.pViewportState->scissorCount);
	w.key("pScissors");
	w.beginArray();
	w.endArray();
	w.comma();
	w.endObject();
	w.comma();

	// Rasterization
	const VkPipelineRasterizationStateCreateInfo* rs = info.pRasterizationState;
	w.key("pRasterizationState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO");
	writePNext(w, rs->pNext, unsupportedStructures);
	w.number("flags", 0);
	w.quoted("depthClampEnable", boolString(rs->depthClampEnable));
	w.quoted("rasterizerDiscardEnable", boolString(rs->rasterizerDiscardEnable));
	w.quoted("polygonMode", toString(rs->polygonMode));
	w.quoted("cullMode", cullModeFlagsToString(rs->cullMode));
	w.quoted("frontFace", toString(rs->frontFace));
	w.quoted("depthBiasEnable", boolString(rs->depthBiasEnable));
	w.real("depthBiasConstantFactor", rs->depthBiasConstantFactor);
	w.real("depthBiasClamp", rs->depthBiasClamp);
	w.real("depthBiasSlopeFactor", rs->depthBiasSlopeFactor);
	w.real("lineWidth", rs->lineWidth);
	w.endObject();
	w.comma();

	// Multisample
	const VkPipelineMultisampleStateCreateInfo* ms = info.pMultisampleState;
	w.key("pMultisampleState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO");
	writePNext(w, ms->pNext, unsupportedStructures);
	w.number("flags", 0);
	w.quoted("rasterizationSamples", sampleCountFlagsToString(ms->rasterizationSamples));
	w.quoted("sampleShadingEnable", boolString(ms->sampleShadingEnable));
	w.real("minSampleShading", ms->minSampleShading);
	w.quoted("pSampleMask", "NULL");
	w.quoted("alphaToCoverageEnable", boolString(ms->alphaToCoverageEnable));
	w.quoted("alphaToOneEnable", boolString(ms->alphaToOneEnable));
	w.endObject();
	w.comma();

	// Depth/stencil
	if (info.pDepthStencilState != nullptr)
	{
		const VkPipelineDepthStencilStateCreateInfo* ds = info.pDepthStencilState;
		w.key("pDepthStencilState");
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO");
		writePNext(w, ds->pNext, unsupportedStructures);
		w.number("flags", 0);
		w.quoted("depthTestEnable", boolString(ds->depthTestEnable));
		w.quoted("depthWriteEnable", boolString(ds->depthWriteEnable));
		w.quoted("depthCompareOp", toString(ds->depthCompareOp));
		w.quoted("depthBoundsTestEnable", boolString(ds->depthBoundsTestEnable));
		w.quoted("stencilTestEnable", boolString(ds->stencilTestEnable));
		writeStencilOpState(w, "front", ds->front);
		writeStencilOpState(w, "back", ds->back);
		w.real("minDepthBounds", ds->minDepthBounds);
		w.real("maxDepthBounds", ds->maxDepthBounds);
		w.endObject();
		w.comma();
	}
	else
	{
		w.quoted("pDepthStencilState", "NULL");
	}

	// Color blend
	const VkPipelineColorBlendStateCreateInfo* cb = info.pColorBlendState;
	w.key("pColorBlendState");
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO");
	writePNext(w, cb->pNext, unsupportedStructures);
	w.number("flags", 0);
	w.quoted("logicOpEnable", boolString(cb->logicOpEnable));
	w.quoted("logicOp", toString(cb->logicOp));
	w.number("attachmentCount", cb->attachmentCount);
	w.key("pAttachments");
	w.beginArray();
	for (uint32_t i = 0; i < cb->attachmentCount; i++)
	{
		const VkPipelineColorBlendAttachmentState& a = cb->pAttachments[i];
		w.pad();
		w.beginObject();
		w.quoted("blendEnable", boolString(a.blendEnable));
		w.quoted("srcColorBlendFactor", toString(a.srcColorBlendFactor));
		w.quoted("dstColorBlendFactor", toString(a.dstColorBlendFactor));
		w.quoted("colorBlendOp", toString(a.colorBlendOp));
		w.quoted("srcAlphaBlendFactor", toString(a.srcAlphaBlendFactor));
		w.quoted("dstAlphaBlendFactor", toString(a.dstAlphaBlendFactor));
		w.quoted("alphaBlendOp", toString(a.alphaBlendOp));
		// As flag names. NVIDIA's PCC reads a hex string or a number here as 0 without
		// complaint, and the pipeline then writes no colour.
		w.quoted("colorWriteMask", colorComponentFlagsToString(a.colorWriteMask));
		w.endObject();
		w.comma();
	}
	w.endArray();
	w.comma();
	w.key("blendConstants");
	w.stream() << "[" << cb->blendConstants[0] << ", " << cb->blendConstants[1] << ", "
	           << cb->blendConstants[2] << ", " << cb->blendConstants[3] << "]";
	w.comma();
	w.endObject();
	w.comma();

	// Dynamic state
	if (info.pDynamicState != nullptr)
	{
		w.key("pDynamicState");
		w.beginObject();
		w.quoted("sType", "VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO");
		writePNext(w, info.pDynamicState->pNext, unsupportedStructures);
		w.number("flags", 0);
		w.number("dynamicStateCount", info.pDynamicState->dynamicStateCount);
		w.key("pDynamicStates");
		w.beginArray();
		for (uint32_t i = 0; i < info.pDynamicState->dynamicStateCount; i++)
		{
			w.pad();
			w.stream() << "\"" << toString(info.pDynamicState->pDynamicStates[i]) << "\"";
			w.comma();
		}
		w.endArray();
		w.comma();
		w.endObject();
		w.comma();
	}
	else
	{
		w.quoted("pDynamicState", "NULL");
	}

	w.key("layout");
	w.stream() << "@LAYOUT@";
	w.comma();
	w.number("renderPass", 0);
	w.number("subpass", info.subpass);
	w.quoted("basePipelineHandle", "");
	w.number("basePipelineIndex", 0);
	w.endObject();

	RecordedPipeline recorded;
	recorded.name = pipelineName;
	recorded.stateJson = w.str();
	recorded.shaderFiles = shaderFiles;
	recorded.stages = stages;
	recorded.layout = info.layout;
	recorded.renderPass = info.renderPass;
	recorded.isCompute = false;
	pipelines.push_back(recorded);
}

void PipelineExporter::recordComputePipeline(const std::string& pipelineName,
                                             const VkComputePipelineCreateInfo& info,
                                             const std::string& shaderFile)
{
	JsonWriter w(3);
	w.beginObject();
	w.quoted("sType", "VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO");
	writePNext(w, info.pNext, unsupportedStructures);
	w.quoted("flags", "0");

	w.key("stage");
	writeShaderStage(w, info.stage, unsupportedStructures);
	w.comma();

	w.key("layout");
	w.stream() << "@LAYOUT@";
	w.comma();
	w.quoted("basePipelineHandle", "");
	w.number("basePipelineIndex", 0);
	w.endObject();

	RecordedPipeline recorded;
	recorded.name = pipelineName;
	recorded.stateJson = w.str();
	recorded.shaderFiles = { shaderFile };
	recorded.stages = { info.stage.stage };
	recorded.layout = info.layout;
	recorded.isCompute = true;
	pipelines.push_back(recorded);
}

namespace
{
// Replaces the first occurrence of a placeholder. The layout and set-layout ids are only known
// once a document is being written, since each document numbers its own objects.
std::string substitute(std::string text, const std::string& token, const std::string& value)
{
	const size_t pos = text.find(token);
	if (pos != std::string::npos)
	{
		text.replace(pos, token.size(), value);
	}
	return text;
}
} // namespace

bool PipelineExporter::writeDocument(const RecordedPipeline& pipeline, const std::string& path) const
{
	const auto layoutIt = pipelineLayouts.find(pipeline.layout);
	if (layoutIt == pipelineLayouts.end())
	{
		std::cerr << "Pipeline export: pipeline \"" << pipeline.name
		          << "\" uses a pipeline layout that was not created through the framework"
		          << std::endl;
		return false;
	}
	const RecordedPipelineLayout& layout = layoutIt->second;

	// Ids are per document. The numbering matches the reference documents: set layouts from 2,
	// the pipeline layout after them.
	constexpr uint32_t kFirstSetLayoutId = 2;
	const uint32_t pipelineLayoutId =
		kFirstSetLayoutId + static_cast<uint32_t>(layout.setLayouts.size()) + 1;

	std::string setLayoutIds = "[";
	for (size_t i = 0; i < layout.setLayouts.size(); i++)
	{
		setLayoutIds += (i ? ", " : "") + std::to_string(kFirstSetLayoutId + i);
	}
	setLayoutIds += "]";

	std::ofstream file(path);
	if (!file.is_open())
	{
		std::cerr << "Pipeline export: could not open " << path << " for writing" << std::endl;
		return false;
	}

	const char* stateKey = pipeline.isCompute ? "ComputePipelineState" : "GraphicsPipelineState";
	const char* pipelineKey = pipeline.isCompute ? "ComputePipeline" : "GraphicsPipeline";

	file << "{\n";
	file << "    \"" << stateKey << "\": {\n";

	// A compute pipeline has no render pass.
	if (!pipeline.isCompute)
	{
		const auto renderPassIt = renderPasses.find(pipeline.renderPass);
		if (renderPassIt == renderPasses.end())
		{
			std::cerr << "Pipeline export: pipeline \"" << pipeline.name
			          << "\" was created against a render pass that was not created through the "
			             "framework"
			          << std::endl;
			return false;
		}
		file << "        \"Renderpass\": " << renderPassIt->second << ",\n";
	}

	file << "        \"DescriptorSetLayouts\": [\n";
	for (size_t i = 0; i < layout.setLayouts.size(); i++)
	{
		const auto dslIt = descriptorSetLayouts.find(layout.setLayouts[i]);
		if (dslIt == descriptorSetLayouts.end())
		{
			std::cerr << "Pipeline export: a descriptor set layout used by \"" << pipeline.name
			          << "\" was not created through the framework" << std::endl;
			return false;
		}
		file << "            {\n";
		file << "                \"" << (kFirstSetLayoutId + i) << "\": " << dslIt->second << "\n";
		file << "            }" << (i + 1 < layout.setLayouts.size() ? "," : "") << "\n";
	}
	file << "        ],\n";

	file << "        \"PipelineLayout\": "
	     << substitute(layout.json, "@SETLAYOUTS@", setLayoutIds) << ",\n";
	file << "        \"" << pipelineKey << "\": "
	     << substitute(pipeline.stateJson, "@LAYOUT@", std::to_string(pipelineLayoutId)) << ",\n";

	file << "        \"ShaderFileNames\": [\n";
	for (size_t i = 0; i < pipeline.shaderFiles.size(); i++)
	{
		file << "            {\n";
		file << "                \"stage\": \"" << shaderStageFlagsToString(pipeline.stages[i])
		     << "\",\n";
		file << "                \"filename\": \"" << pipeline.shaderFiles[i] << "\"\n";
		file << "            }" << (i + 1 < pipeline.shaderFiles.size() ? "," : "") << "\n";
	}
	file << "        ]\n";
	file << "    },\n";

	file << "    \"EnabledExtensions\": [],\n";

	uint8_t identifier[16];
	computePipelineIdentifier(sampleName + "/" + pipeline.name, identifier);
	file << "    \"PipelineUUID\": [";
	for (int i = 0; i < 16; i++)
	{
		file << (i ? ", " : "") << static_cast<int>(identifier[i]);
	}
	file << "]\n";
	file << "}\n";

	return true;
}

bool PipelineExporter::write() const
{
	if (pipelines.empty())
	{
		std::cerr << "Pipeline export: the sample recorded no pipelines" << std::endl;
		return false;
	}
	if (!unsupportedStructures.empty())
	{
		for (const std::string& sType : unsupportedStructures)
		{
			std::cerr << "Pipeline export: the sample chains " << sType
			          << ", which the exporter cannot write; add it to writePNext()" << std::endl;
		}
		return false;
	}

	// One document per pipeline. The pipeline cache compiler takes them all through its --prefix
	// option, so the names have to share the "<sample>.pc" stem.
	// Warzone 2100: named "<sample>.pc.<identifier in hex>.json" (was ".pc", ".pc1", ... in record
	// order), so that a pipeline has the same file in every export run, and runs that reach
	// different pipelines can write into one directory.
	for (size_t i = 0; i < pipelines.size(); i++)
	{
		uint8_t identifier[16];
		computePipelineIdentifier(sampleName + "/" + pipelines[i].name, identifier);
		static const char hexDigits[] = "0123456789abcdef";
		std::string path = outputDirectory + "/" + sampleName + ".pc.";
		for (const uint8_t byte : identifier)
		{
			path += hexDigits[byte >> 4];
			path += hexDigits[byte & 0xF];
		}
		path += ".json";

		if (!writeDocument(pipelines[i], path))
		{
			return false;
		}
		std::cout << "Wrote " << path << " (" << pipelines[i].name << ")" << std::endl;
	}
	return true;
}

} // namespace vks
