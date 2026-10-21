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

// WebGPU backend (Dawn).
//
// This is the first stage of the backend: it creates the device and the surface,
// and it presents a cleared frame each screen frame. Textures, buffers and
// pipelines are no-op objects, as in the null backend, and shouldDraw() is false,
// so the render graph does not run yet.
//
// This file must build as C++20, because webgpu_cpp.h needs C++20.

#if defined(WZ_WEBGPU_ENABLED)

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "gfx_api_webgpu.h"
#include "shader_catalog.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <set>
#include <string_view>

namespace
{

std::string to_std_string(const wgpu::StringView& value)
{
	return std::string(std::string_view(value));
}

const char* to_string(wgpu::BackendType type)
{
	switch (type)
	{
		case wgpu::BackendType::Undefined: return "Undefined";
		case wgpu::BackendType::Null: return "Null";
		case wgpu::BackendType::WebGPU: return "WebGPU";
		case wgpu::BackendType::D3D11: return "D3D11";
		case wgpu::BackendType::D3D12: return "D3D12";
		case wgpu::BackendType::Metal: return "Metal";
		case wgpu::BackendType::Vulkan: return "Vulkan";
		case wgpu::BackendType::OpenGL: return "OpenGL";
		case wgpu::BackendType::OpenGLES: return "OpenGL ES";
	}
	return "Unknown";
}

const char* to_string(wgpu::AdapterType type)
{
	switch (type)
	{
		case wgpu::AdapterType::DiscreteGPU: return "Discrete GPU";
		case wgpu::AdapterType::IntegratedGPU: return "Integrated GPU";
		case wgpu::AdapterType::CPU: return "CPU";
		case wgpu::AdapterType::Unknown: return "Unknown";
	}
	return "Unknown";
}

// MARK: wgpu_texture (no-op for now)

struct wgpu_texture final : public gfx_api::texture
{
	gfx_api::pixel_format internal_format = gfx_api::pixel_format::invalid;
	size_t tex_width = 0;
	size_t tex_height = 0;

	void bind() override {}

	bool upload(const size_t& mip_level, const iV_BaseImage& image) override
	{
		ASSERT_OR_RETURN(false, image.data() != nullptr, "Attempt to upload image without data");
		ASSERT_OR_RETURN(false, image.pixel_format() == internal_format, "Uploading image to texture with different format");
		return true;
	}

	bool upload_sub(const size_t& mip_level, const size_t& offset_x, const size_t& offset_y, const iV_BaseImage& image) override
	{
		ASSERT_OR_RETURN(false, image.data() != nullptr, "Attempt to upload image without data");
		ASSERT_OR_RETURN(false, image.pixel_format() == internal_format, "Uploading image to texture with different format");
		return true;
	}

	unsigned id() override { return 0; }
	gfx_api::texture2dDimensions get_dimensions() const override { return {tex_width, tex_height}; }
	size_t backend_internal_value() const override { return 0; }
};

// MARK: wgpu_texture_array (no-op for now)

struct wgpu_texture_array final : public gfx_api::texture_array
{
	gfx_api::pixel_format internal_format = gfx_api::pixel_format::invalid;

	void bind() override {}

	bool upload_layer(const size_t& layer, const size_t& mip_level, const iV_BaseImage& image) override
	{
		ASSERT_OR_RETURN(false, image.data() != nullptr, "Attempt to upload image without data");
		ASSERT_OR_RETURN(false, image.pixel_format() == internal_format, "Uploading image to texture with different format");
		return true;
	}

	unsigned id() override { return 0; }
	size_t backend_internal_value() const override { return 0; }
};

// MARK: wgpu_buffer (no-op for now)

struct wgpu_buffer final : public gfx_api::buffer
{
	gfx_api::buffer::usage usage;
	size_t buffer_size = 0;

	explicit wgpu_buffer(const gfx_api::buffer::usage& _usage)
	: usage(_usage)
	{ }

	void bind() override {}

	void upload(const size_t& size, const void* data) override
	{
		ASSERT(size > 0, "Attempt to upload buffer of size 0");
		buffer_size = size;
	}

	void update(const size_t& start, const size_t& size, const void* data, const update_flag flag = update_flag::none) override
	{
		ASSERT(start < buffer_size, "Starting offset (%zu) is past end of buffer (length: %zu)", start, buffer_size);
		ASSERT(start + size <= buffer_size, "Attempt to write past end of buffer");
	}

	size_t current_buffer_size() override { return buffer_size; }
};

// MARK: wgpu_pipeline_state_object (no-op for now)

struct wgpu_pipeline_state_object final : public gfx_api::pipeline_state_object
{
};

// MARK: wgpu_context

struct wgpu_context final : public gfx_api::context
{
public:
	explicit wgpu_context(bool _useDebug)
	: useDebug(_useDebug)
	{ }
	~wgpu_context() override;

	gfx_api::texture* create_texture(const size_t& mipmap_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename) override;
	gfx_api::texture_array* create_texture_array(const size_t& mipmap_count, const size_t& layer_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename) override;
	gfx_api::buffer* create_buffer_object(const gfx_api::buffer::usage& usage, const buffer_storage_hint& hint = buffer_storage_hint::static_draw, const std::string& debugName = "") override;

	gfx_api::pipeline_state_object* build_pipeline(gfx_api::pipeline_state_object* existing_pso, const gfx_api::pipeline_create_info& createInfo) override;
	void bind_pipeline(gfx_api::pipeline_state_object* pso, bool notextures) override {}
	void bind_index_buffer(gfx_api::buffer&, const gfx_api::index_type&) override {}
	void unbind_index_buffer(gfx_api::buffer&) override {}
	void bind_vertex_buffers(const std::size_t& first, const std::vector<std::tuple<gfx_api::buffer*, std::size_t>>& vertex_buffers_offset) override {}
	void unbind_vertex_buffers(const std::size_t& first, const std::vector<std::tuple<gfx_api::buffer*, std::size_t>>& vertex_buffers_offset) override {}
	void disable_all_vertex_buffers() override {}
	void bind_streamed_vertex_buffers(const void* data, const std::size_t size) override {}
	void bind_textures(const std::vector<gfx_api::texture_input>& texture_descriptions, const std::vector<gfx_api::abstract_texture*>& textures) override {}
	void set_constants(const void* buffer, const size_t& size) override {}
	void set_uniforms(const size_t& first, const std::vector<std::tuple<const void*, size_t>>& uniform_blocks) override {}
	gfx_api::frame_uniform_allocation upload_frame_uniform_raw(const void* data, size_t size) override;
	void set_frame_uniform_at(size_t slot, const gfx_api::frame_uniform_allocation& allocation, std::type_index type) override {}
	void draw(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive) override {}
	void draw_elements(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index) override {}
	void set_polygon_offset(const float& factor, const float& units) override {}
	void set_depth_range(const float& min, const float& max) override {}
	int32_t get_context_value(const context_value property) override;
	uint64_t get_estimated_vram_mb(bool dedicatedOnly) override { return 0; }
	void beginPass(const gfx_api::RenderPassDesc& pass, const gfx_api::CompiledPass* compiledPass = nullptr) override {}
	void endPass(const gfx_api::CompiledPass* compiledPass = nullptr) override {}
	void reconcileSwapchainAtFrameOpen() override;
	void beginScreenFrame() override;
	void finishScreenFrame() override;
	bool ensurePipelineSurfaces(const gfx_api::ResolvedSurfaceTable& specs) override { return true; }
	void warmCompiledRenderGraph(std::vector<gfx_api::RenderPassDesc>& passes, gfx_api::PassGraphCompileResult& compileResult) override {}
	void debugStringMarker(const char* str) override {}
	void debugSceneBegin(const char* descr) override {}
	void debugSceneEnd(const char* descr) override {}
	bool debugPerfAvailable() override { return false; }
	bool debugPerfStart(size_t sample) override { return false; }
	void debugPerfStop() override {}
	void debugPerfBegin(PERF_POINT pp, const char* descr) override {}
	void debugPerfEnd(PERF_POINT pp) override {}
	uint64_t debugGetPerfValue(PERF_POINT pp) override { return 0; }
	std::map<std::string, std::string> getBackendGameInfo() override { return backendGameInfo; }
	const std::string& getFormattedRendererInfoString() const override { return formattedRendererInfoString; }
	bool getScreenshot(std::function<void (std::unique_ptr<iV_Image>)> callback) override { return false; }
	void handleWindowSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight) override;
	std::pair<uint32_t, uint32_t> getDrawableDimensions() override;
	// The WebGPU shaders negate Y, so that they render the same image as Vulkan (see the vertex shaders)
	bool isYAxisInverted() const override { return true; }
	// The render graph does not run until the backend can draw (see the comment at the top of this file)
	bool shouldDraw() override { return false; }
	void shutdown() override;
	const size_t& current_FrameNum() const override { return frameNum; }
	bool setSwapInterval(gfx_api::context::swap_interval_mode mode, const SetSwapIntervalCompletionHandler& completionHandler) override;
	gfx_api::context::swap_interval_mode getSwapInterval() const override { return swapMode; }
	bool textureFormatIsSupported(gfx_api::pixel_format_target target, gfx_api::pixel_format format, gfx_api::pixel_format_usage::flags usage) override { return true; }
	bool supportsMipLodBias() const override { return false; }
	bool supports2DTextureArrays() const override { return true; }
	bool supportsIntVertexAttributes() const override { return true; }
	size_t maxFramesInFlight() const override { return 1; }
	gfx_api::lighting_constants getShadowConstants() override { return shadowConstants; }
	bool setShadowConstants(gfx_api::lighting_constants values) override { shadowConstants = values; return true; }
	bool supportsInstancedRendering() override { return false; }
	void draw_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, std::size_t instance_count) override {}
	void draw_elements_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index, std::size_t instance_count) override {}
	bool debugRecompileAllPipelines() override { return true; }

private:
	bool _initialize(const gfx_api::backend_Impl_Factory& impl, int32_t antialiasing, swap_interval_mode mode, optional<float> mipLodBias, uint32_t depthMapResolution) override;
	bool requestAdapter();
	bool requestDevice();
	bool readSurfaceCapabilities();
	optional<wgpu::PresentMode> presentModeFor(swap_interval_mode mode) const;
	void configureSurface();
	void presentClearedFrame();
	bool compileAllShaderModules();
	void releaseWebGPUObjects();

private:
	bool useDebug = false; // --gfxdebug
	std::unique_ptr<gfx_api::backend_WebGPU_Impl> backend_impl;

	// Declaration order matters: members are destroyed in reverse order, and the
	// surface must be released before the device (see releaseWebGPUObjects).
	wgpu::Instance instance;
	wgpu::Adapter adapter;
	wgpu::Device device;
	wgpu::Queue queue;
	wgpu::Surface surface;

	wgpu::Limits deviceLimits;
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
	wgpu::TextureUsage surfaceUsage = wgpu::TextureUsage::RenderAttachment;
	std::vector<wgpu::PresentMode> supportedPresentModes;
	bool surfaceConfigured = false;
	bool surfaceNeedsConfigure = false;
	bool deviceLost = false;

	swap_interval_mode swapMode = swap_interval_mode::vsync;
	gfx_api::lighting_constants shadowConstants;
	std::map<std::string, std::string> backendGameInfo;
	std::string formattedRendererInfoString = "WebGPU";
	size_t frameNum = 0;
	size_t presentedFrames = 0;
};

wgpu_context::~wgpu_context()
{
	releaseWebGPUObjects();
}

// MARK: wgpu_context - initialization

bool wgpu_context::_initialize(const gfx_api::backend_Impl_Factory& impl, int32_t antialiasing, swap_interval_mode mode, optional<float> mipLodBias, uint32_t depthMapResolution)
{
	backend_impl = impl.createWebGPUBackendImpl();
	if (!backend_impl)
	{
		debug(LOG_ERROR, "Failed to get the WebGPU backend implementation");
		return false;
	}

	static const wgpu::InstanceFeatureName instanceFeatures[] = { wgpu::InstanceFeatureName::TimedWaitAny };
	wgpu::InstanceDescriptor instanceDesc;
	instanceDesc.requiredFeatureCount = 1;
	instanceDesc.requiredFeatures = instanceFeatures;
	instance = wgpu::CreateInstance(&instanceDesc);
	if (!instance)
	{
		debug(LOG_ERROR, "Failed to create the WebGPU instance");
		return false;
	}

	std::string surfaceError;
	WGPUSurface rawSurface = backend_impl->createSurface(instance.Get(), surfaceError);
	if (rawSurface == nullptr)
	{
		debug(LOG_ERROR, "Failed to create the WebGPU surface: %s", surfaceError.c_str());
		return false;
	}
	surface = wgpu::Surface::Acquire(rawSurface);

	if (!requestAdapter())
	{
		return false;
	}

	// A bad surface descriptor gives an error object, not a null surface,
	// so check the surface capabilities to find out if the surface works
	if (!readSurfaceCapabilities())
	{
		return false;
	}

	if (!requestDevice())
	{
		return false;
	}

	if (!presentModeFor(mode).has_value())
	{
		debug(LOG_3D, "Swap interval %d is not supported; defaulting to vsync on", to_int(mode));
		mode = swap_interval_mode::vsync;
	}
	swapMode = mode;
	configureSurface();

	if (useDebug && !compileAllShaderModules())
	{
		debug(LOG_ERROR, "Some WebGPU shaders failed to compile");
	}

	return true;
}

bool wgpu_context::requestAdapter()
{
	wgpu::RequestAdapterOptions options;
	options.compatibleSurface = surface;
	instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
		[this](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
			if (status == wgpu::RequestAdapterStatus::Success)
			{
				adapter = std::move(result);
			}
			else
			{
				debug(LOG_ERROR, "WebGPU RequestAdapter failed: %s", to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	// The options hold a reference to the surface. It must not outlive the device.
	options.compatibleSurface = nullptr;

	if (!adapter)
	{
		return false;
	}

	wgpu::AdapterInfo info;
	adapter.GetInfo(&info);
	backendGameInfo["webgpu_backend"] = to_string(info.backendType);
	backendGameInfo["webgpu_adapter_type"] = to_string(info.adapterType);
	backendGameInfo["webgpu_vendor"] = to_std_string(info.vendor);
	backendGameInfo["webgpu_device"] = to_std_string(info.device);
	backendGameInfo["webgpu_driver"] = to_std_string(info.description);
	formattedRendererInfoString = "WebGPU (" + backendGameInfo["webgpu_backend"] + "): " + backendGameInfo["webgpu_device"];
	debug(LOG_INFO, "WebGPU adapter: %s (%s), %s backend", backendGameInfo["webgpu_device"].c_str(), backendGameInfo["webgpu_driver"].c_str(), backendGameInfo["webgpu_backend"].c_str());
	return true;
}

bool wgpu_context::readSurfaceCapabilities()
{
	wgpu::SurfaceCapabilities capabilities;
	if (surface.GetCapabilities(adapter, &capabilities) != wgpu::Status::Success || capabilities.formatCount == 0)
	{
		debug(LOG_ERROR, "The WebGPU surface is not usable with the adapter");
		return false;
	}

	// Prefer a unorm format: Dawn can report an sRGB format first
	surfaceFormat = capabilities.formats[0];
	for (size_t i = 0; i < capabilities.formatCount; ++i)
	{
		if (capabilities.formats[i] == wgpu::TextureFormat::BGRA8Unorm || capabilities.formats[i] == wgpu::TextureFormat::RGBA8Unorm)
		{
			surfaceFormat = capabilities.formats[i];
			break;
		}
	}

	supportedPresentModes.assign(capabilities.presentModes, capabilities.presentModes + capabilities.presentModeCount);

	surfaceUsage = wgpu::TextureUsage::RenderAttachment;
	if (capabilities.usages & wgpu::TextureUsage::CopySrc)
	{
		// For screenshots
		surfaceUsage |= wgpu::TextureUsage::CopySrc;
	}
	return true;
}

bool wgpu_context::requestDevice()
{
	// Ask for the full limits of the adapter: terrain needs at least 256 array layers,
	// and the texture size limits feed MAX_TEXTURE_SIZE
	wgpu::Limits adapterLimits;
	adapter.GetLimits(&adapterLimits);

	wgpu::DeviceDescriptor deviceDesc;
	deviceDesc.label = "Warzone 2100";
	deviceDesc.requiredLimits = &adapterLimits;
	deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
		debug(LOG_ERROR, "WebGPU error (%d): %s", static_cast<int>(type), to_std_string(message).c_str());
	});
	deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
		[](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message, wgpu_context* self) {
			if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled)
			{
				return; // expected at shutdown
			}
			self->deviceLost = true;
			debug(LOG_ERROR, "WebGPU device lost (%d): %s", static_cast<int>(reason), to_std_string(message).c_str());
		}, this);

	instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
		[this](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
			if (status == wgpu::RequestDeviceStatus::Success)
			{
				device = std::move(result);
			}
			else
			{
				debug(LOG_ERROR, "WebGPU RequestDevice failed: %s", to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	if (!device)
	{
		return false;
	}

	device.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
		const code_part part = (type == wgpu::LoggingType::Error) ? LOG_ERROR : ((type == wgpu::LoggingType::Warning) ? LOG_WARNING : LOG_3D);
		debug(part, "Dawn: %s", to_std_string(message).c_str());
	});

	queue = device.GetQueue();
	device.GetLimits(&deviceLimits);
	return true;
}

// MARK: wgpu_context - surface and frames

optional<wgpu::PresentMode> wgpu_context::presentModeFor(swap_interval_mode mode) const
{
	auto isSupported = [this](wgpu::PresentMode presentMode) {
		return std::find(supportedPresentModes.begin(), supportedPresentModes.end(), presentMode) != supportedPresentModes.end();
	};
	switch (mode)
	{
		case swap_interval_mode::immediate:
			if (isSupported(wgpu::PresentMode::Immediate))
			{
				return wgpu::PresentMode::Immediate;
			}
			if (isSupported(wgpu::PresentMode::Mailbox))
			{
				return wgpu::PresentMode::Mailbox;
			}
			return nullopt;
		case swap_interval_mode::vsync:
			return wgpu::PresentMode::Fifo; // always supported
		case swap_interval_mode::adaptive_vsync:
			if (isSupported(wgpu::PresentMode::FifoRelaxed))
			{
				return wgpu::PresentMode::FifoRelaxed;
			}
			return nullopt;
	}
	return nullopt;
}

void wgpu_context::configureSurface()
{
	surfaceNeedsConfigure = false;

	int width = 0;
	int height = 0;
	backend_impl->getDrawableSize(&width, &height);
	if (width <= 0 || height <= 0)
	{
		// For example, the window is minimized. Try again when the size changes.
		surfaceConfigured = false;
		return;
	}

	wgpu::SurfaceConfiguration config;
	config.device = device;
	config.format = surfaceFormat;
	config.usage = surfaceUsage;
	config.width = static_cast<uint32_t>(width);
	config.height = static_cast<uint32_t>(height);
	config.presentMode = presentModeFor(swapMode).value_or(wgpu::PresentMode::Fifo);
	config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
	surface.Configure(&config);
	surfaceConfigured = true;
	debug(LOG_3D, "Configured the WebGPU surface: %dx%d, format %d, present mode %d", width, height, static_cast<int>(config.format), static_cast<int>(config.presentMode));
}

void wgpu_context::reconcileSwapchainAtFrameOpen()
{
	if (device && surfaceNeedsConfigure)
	{
		configureSurface();
	}
}

void wgpu_context::beginScreenFrame()
{
	purgeFrameResources();
	advanceFrameUniformGeneration();
}

void wgpu_context::presentClearedFrame()
{
	wgpu::SurfaceTexture surfaceTexture;
	surface.GetCurrentTexture(&surfaceTexture);
	switch (surfaceTexture.status)
	{
		case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
			break;
		case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
			surfaceNeedsConfigure = true; // draw this frame, configure again at the next frame
			break;
		case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
		case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
		case wgpu::SurfaceGetCurrentTextureStatus::Lost:
			surfaceNeedsConfigure = true;
			return;
		case wgpu::SurfaceGetCurrentTextureStatus::Error:
			debug(LOG_ERROR, "Failed to get the current WebGPU surface texture");
			return;
	}

	wgpu::RenderPassColorAttachment colorAttachment;
	colorAttachment.view = surfaceTexture.texture.CreateView();
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	// A slowly pulsing blue, so that it is easy to see that frames are presented.
	// Temporary: the render graph draws the frame once shouldDraw() is true.
	const double pulse = static_cast<double>(frameNum % 120) / 120.0;
	colorAttachment.clearValue = {0.05, 0.10 + 0.15 * pulse, 0.30 + 0.30 * pulse, 1.0};

	wgpu::RenderPassDescriptor passDesc;
	passDesc.label = "WebGPU clear";
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;

	wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
	encoder.BeginRenderPass(&passDesc).End();
	wgpu::CommandBuffer commands = encoder.Finish();
	queue.Submit(1, &commands);
#if !defined(__EMSCRIPTEN__)
	// In the browser, the frame is presented at the end of the animation frame
	surface.Present();
#endif
	++presentedFrames;
	if (presentedFrames == 1)
	{
		debug(LOG_3D, "Presented the first WebGPU frame");
	}
}

void wgpu_context::finishScreenFrame()
{
	if (device && surfaceConfigured && !deviceLost)
	{
		presentClearedFrame();
	}
	if (instance)
	{
		instance.ProcessEvents();
	}
	frameNum = std::max<size_t>(frameNum + 1, 1);
	purgeFrameResources();
}

void wgpu_context::handleWindowSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
	surfaceNeedsConfigure = true;
	markScreenGeometryDirty();
}

std::pair<uint32_t, uint32_t> wgpu_context::getDrawableDimensions()
{
	int width = 0;
	int height = 0;
	if (backend_impl)
	{
		backend_impl->getDrawableSize(&width, &height);
	}
	return {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
}

bool wgpu_context::setSwapInterval(gfx_api::context::swap_interval_mode mode, const SetSwapIntervalCompletionHandler& completionHandler)
{
	if (!presentModeFor(mode).has_value())
	{
		return false;
	}
	swapMode = mode;
	surfaceNeedsConfigure = true; // the present mode changes at the next frame
	if (completionHandler)
	{
		completionHandler();
	}
	return true;
}

// MARK: wgpu_context - resources (no-op objects for now)

gfx_api::texture* wgpu_context::create_texture(const size_t& mipmap_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename)
{
	auto* new_texture = new wgpu_texture();
	new_texture->internal_format = internal_format;
	new_texture->tex_width = width;
	new_texture->tex_height = height;
	return new_texture;
}

gfx_api::texture_array* wgpu_context::create_texture_array(const size_t& mipmap_count, const size_t& layer_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename)
{
	ASSERT(mipmap_count > 0, "mipmap_count must be > 0");
	auto* new_texture = new wgpu_texture_array();
	new_texture->internal_format = internal_format;
	return new_texture;
}

gfx_api::buffer* wgpu_context::create_buffer_object(const gfx_api::buffer::usage& usage, const buffer_storage_hint& hint, const std::string& debugName)
{
	return new wgpu_buffer(usage);
}

gfx_api::pipeline_state_object* wgpu_context::build_pipeline(gfx_api::pipeline_state_object* existing_pso, const gfx_api::pipeline_create_info& createInfo)
{
	return new wgpu_pipeline_state_object();
}

gfx_api::frame_uniform_allocation wgpu_context::upload_frame_uniform_raw(const void* data, size_t size)
{
	// Nothing is uploaded yet, but the reference must pass the checks its consumers make
	gfx_api::frame_uniform_allocation result;
	result.handle = 1;
	result.size = static_cast<uint32_t>(size);
	result.generation = frameUniformGeneration();
	return result;
}

int32_t wgpu_context::get_context_value(const context_value property)
{
	switch (property)
	{
		case gfx_api::context::context_value::MAX_TEXTURE_SIZE:
			return static_cast<int32_t>(deviceLimits.maxTextureDimension2D);
		case gfx_api::context::context_value::MAX_SAMPLES:
			return 4; // WebGPU supports only 1 and 4 samples
		case gfx_api::context::context_value::MAX_ARRAY_TEXTURE_LAYERS:
			return static_cast<int32_t>(deviceLimits.maxTextureArrayLayers);
		case gfx_api::context::context_value::MAX_VERTEX_ATTRIBS:
			return static_cast<int32_t>(deviceLimits.maxVertexAttributes);
		case gfx_api::context::context_value::MAX_VERTEX_OUTPUT_COMPONENTS:
			return static_cast<int32_t>(deviceLimits.maxInterStageShaderVariables * 4);
		case gfx_api::context::context_value::MAX_TESS_GEN_LEVEL:
			return 0; // WebGPU has no tessellation
	}
	debug(LOG_FATAL, "Unsupported property");
	return 0;
}

// MARK: wgpu_context - shader check

// With --gfxdebug, compile the WGSL of every shader program that WebGPU supports, and report
// the errors. This checks the translation, the packing into base.wz, and Dawn's validation.
bool wgpu_context::compileAllShaderModules()
{
	std::set<std::string> paths;
	for (int mode = static_cast<int>(SHADER_NONE) + 1; mode < static_cast<int>(SHADER_MAX); ++mode)
	{
		const gfx_api::shader_program_desc& desc = gfx_api::shader_catalog(static_cast<SHADER_MODE>(mode));
		if (desc.stage_count == 0 || !gfx_api::shader_supported_by_webgpu(desc))
		{
			continue;
		}
		for (uint8_t i = 0; i < desc.stage_count; ++i)
		{
			paths.insert(gfx_api::wgsl_path(desc.stages[i].basename));
		}
	}

	size_t failures = 0;
	for (const std::string& path : paths)
	{
		PHYSFS_file* file = PHYSFS_openRead(path.c_str());
		if (file == nullptr)
		{
			debug(LOG_ERROR, "Missing WebGPU shader: %s", path.c_str());
			++failures;
			continue;
		}
		const PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
		std::string code(static_cast<size_t>(std::max<PHYSFS_sint64>(fileSize, 0)), '\0');
		const bool readOk = (fileSize > 0) && (WZ_PHYSFS_readBytes(file, code.data(), static_cast<PHYSFS_uint32>(fileSize)) == fileSize);
		PHYSFS_close(file);
		if (!readOk)
		{
			debug(LOG_ERROR, "Failed to read WebGPU shader: %s", path.c_str());
			++failures;
			continue;
		}

		wgpu::ShaderSourceWGSL source;
		source.code = code.c_str();
		wgpu::ShaderModuleDescriptor moduleDesc;
		moduleDesc.nextInChain = &source;
		moduleDesc.label = path.c_str();
		wgpu::ShaderModule module = device.CreateShaderModule(&moduleDesc);

		bool hasError = false;
		instance.WaitAny(module.GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
			[&](wgpu::CompilationInfoRequestStatus, const wgpu::CompilationInfo* compilationInfo) {
				for (size_t m = 0; compilationInfo != nullptr && m < compilationInfo->messageCount; ++m)
				{
					const wgpu::CompilationMessage& message = compilationInfo->messages[m];
					if (message.type == wgpu::CompilationMessageType::Error)
					{
						hasError = true;
						debug(LOG_ERROR, "%s:%llu: %s", path.c_str(), static_cast<unsigned long long>(message.lineNum), to_std_string(message.message).c_str());
					}
				}
			}), UINT64_MAX);
		failures += hasError ? 1 : 0;
	}

	debug(LOG_INFO, "WebGPU shader check: %zu shader modules, %zu failed", paths.size(), failures);
	return failures == 0;
}

// MARK: wgpu_context - shutdown

void wgpu_context::releaseWebGPUObjects()
{
	// Release the surface first. If the device goes first, Dawn's Vulkan
	// swapchain teardown crashes in FencedDeleter.
	if (surface && surfaceConfigured)
	{
		surface.Unconfigure();
	}
	surfaceConfigured = false;
	surface = nullptr;
	queue = nullptr;
	device = nullptr;
	adapter = nullptr;
	instance = nullptr;
}

void wgpu_context::shutdown()
{
	debug(LOG_3D, "WebGPU backend shutdown: %zu frames presented", presentedFrames);
	releaseWebGPUObjects();
	backend_impl.reset();
}

} // anonymous namespace

gfx_api::context* gfx_api::createWebGPUContext(bool useDebug)
{
	return new wgpu_context(useDebug);
}

#endif // defined(WZ_WEBGPU_ENABLED)
