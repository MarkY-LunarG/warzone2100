# Pipeline JSON exporter from vulkansc-samples

These files come from `base/` of the LunarG `vulkansc-samples` repository
(<https://github.com/LunarG/vulkansc-samples>), commit `6ea064d`. They are MIT licensed, as their
headers say.

The Vulkan SC build of Warzone 2100 needs every pipeline compiled offline by a pipeline cache
compiler, which reads one Khronos pipeline JSON document per pipeline. The desktop Vulkan backend
records the create info of each render pass, descriptor set layout, pipeline layout, and pipeline,
and these files write the documents.

| File | Purpose |
|---|---|
| `VulkanSCPipelineExport.h`, `VulkanSCPipelineExport.cpp` | Records create infos by handle and writes one document per pipeline |
| `VulkanSCPipelineIdentifier.h` | The 16-byte pipeline identifier: two FNV-1a 64-bit hashes of the pipeline name |
| `VulkanSCEnumStrings.h` | Enum value names, generated from `vulkan_sc_core.h` by `scripts/gen_enum_strings.py` |
| `vulkansc_compat.h` | Local: selects the Vulkan header (below) |

## Local changes

1. `VulkanSCPipelineExport.h` and `VulkanSCEnumStrings.h` include `vulkansc_compat.h` instead of
   `<vulkan/vulkan_sc.h>`. The desktop build exports the JSON, and `vulkan.h` and `vulkan_sc.h`
   cannot share a translation unit. `vulkansc_compat.h` includes `vulkan.h` plus the three Vulkan SC
   declarations the exporter uses (or `vulkan_sc.h`, if `WZ_VULKANSC_EXPORTER_USE_VULKAN_SC_H` is
   defined; Warzone 2100 does not define it, because its Vulkan SC build also uses the desktop headers).
2. `pResolveAttachments` is written out, for both render pass create info forms. The original
   always wrote `"NULL"`, which is wrong for the multisampled scene passes.
3. Each document is named `<name>.pc.<identifier in hex>.json` instead of `<name>.pc.json`,
   `<name>.pc1.json`, and so on in record order. A pipeline then has the same file in every export
   run, and runs that reach different pipelines can write into one directory.

The enum tables are generated from the Vulkan SC header, so they hold only values that Vulkan SC
has. A value that is not in a table is written as a number instead of a name, so a desktop
pipeline that used anything Vulkan SC does not have shows up as a bare number in its document.
