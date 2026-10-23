/*
* Pipeline JSON export
*
* Vulkan SC compiles pipelines ahead of time, so every sample needs pipeline JSON describing its
* render pass, descriptor set layouts, pipeline layout and full pipeline state for the pipeline
* cache compiler to consume. Writing that by hand for each sample is slow and, worse, silently
* fragile: any field that disagrees with what the code actually builds produces a pipeline that
* does not match at run time, with nothing pointing at the field that is wrong.
*
* So the code writes it instead. In export mode the sample runs normally, but the framework records
* the create-info structs it is handed and serializes them, rather than creating pipelines. The
* JSON is by construction exactly what the code builds.
*
* This works because a Vulkan SC device can be created with no pipeline cache declared at all
* (pipelineCacheCreateInfoCount = 0), which is what breaks the chicken-and-egg: the export run
* needs a device to build state against, but has no cache yet because the cache is what it is
* generating the input for.
*
* One document is written per pipeline -- <sample>.pc.json, <sample>.pc1.json, ... -- because each
* is self-contained and the pipeline cache compiler selects them all through its --prefix option.
* Objects are tracked by handle so that a sample whose pipelines have different descriptor set
* layouts gets the right ones in each document.
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vulkansc_compat.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vks
{

class PipelineExporter
{
  public:
	void setSampleName(const std::string& name) { sampleName = name; }
	void setOutputDirectory(const std::string& dir) { outputDirectory = dir; }

	// Recorded as the sample builds its state. The handle is what ties a pipeline to the layout
	// and descriptor set layouts it actually uses.
	void recordRenderPass(const VkRenderPassCreateInfo& info, VkRenderPass handle);

	// The VkRenderPassCreateInfo2 form, for a sample that needs something only the second
	// structure can express -- a fragment shading rate attachment is the case here, since
	// VkFragmentShadingRateAttachmentInfoKHR chains onto a VkSubpassDescription2 and has nowhere
	// to attach in the first form.
	//
	// Worth knowing when reading the output: the emulation stack's pipeline cache compiler does
	// not parse this node at all. It reads PipelineUUID, EnabledExtensions, the stage count,
	// pStages and ShaderFileNames, and stores the rest of the document verbatim for the ICD, which
	// builds the pipeline from the application's own create info. A vendor compiler that really
	// compiles from the JSON would read it, so it is written out properly rather than stubbed.
	void recordRenderPass2(const VkRenderPassCreateInfo2& info, VkRenderPass handle);
	void recordDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& info,
	                               VkDescriptorSetLayout handle);
	void recordPipelineLayout(const VkPipelineLayoutCreateInfo& info, VkPipelineLayout handle);

	// shaderFiles is in pStages order for graphics; compute takes the single stage's file.
	void recordGraphicsPipeline(const std::string& pipelineName,
	                            const VkGraphicsPipelineCreateInfo& info,
	                            const std::vector<std::string>& shaderFiles);
	void recordComputePipeline(const std::string& pipelineName,
	                           const VkComputePipelineCreateInfo& info,
	                           const std::string& shaderFile);

	// Writes one document per recorded pipeline. Returns false and explains on failure.
	bool write() const;

  private:
	struct RecordedPipeline
	{
		std::string name;
		std::string stateJson;         // the GraphicsPipeline / ComputePipeline node
		std::vector<std::string> shaderFiles;
		std::vector<VkShaderStageFlagBits> stages;
		VkPipelineLayout layout{ VK_NULL_HANDLE };
		// The render pass this pipeline was created against. A sample with an offscreen target
		// has more than one, and each document has to carry the right one.
		VkRenderPass renderPass{ VK_NULL_HANDLE };
		bool isCompute{ false };
	};

	struct RecordedPipelineLayout
	{
		std::string json;                                  // with set-layout ids substituted
		std::vector<VkDescriptorSetLayout> setLayouts;
	};

	// Writes one pipeline's document. Returns false on failure.
	bool writeDocument(const RecordedPipeline& pipeline, const std::string& path) const;

	std::string sampleName;
	std::string outputDirectory;
	std::map<VkRenderPass, std::string> renderPasses;

	std::map<VkDescriptorSetLayout, std::string> descriptorSetLayouts;
	std::map<VkPipelineLayout, RecordedPipelineLayout> pipelineLayouts;
	std::vector<RecordedPipeline> pipelines;

	// The sTypes of chained structures the exporter has no writer for. write() fails if any were
	// seen, rather than leave them out of the document.
	std::set<std::string> unsupportedStructures;
};

} // namespace vks
