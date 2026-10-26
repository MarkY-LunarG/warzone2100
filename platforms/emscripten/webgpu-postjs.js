/* WebGPU backend: create the WebGPU device before the game starts.
 *
 * Messages use err() (Emscripten's stderr), so that a build linked with --emrun forwards them
 * to the emrun terminal.
 *
 * In the browser, adapter and device creation is asynchronous, but the game starts its
 * graphics synchronously. Thus, a preRun step requests the device and holds the start
 * (addRunDependency) until the device exists. The backend gets the device with
 * emscripten_webgpu_get_device(), which reads Module['preinitializedWebGPUDevice'].
 * If WebGPU is not available, the game starts without the device and uses WebGL.
 */
async function wzCreateWebGPUDevice() {
	if (!navigator.gpu) {
		err('WebGPU: navigator.gpu is not available; WebGPU is disabled');
		return;
	}
	const adapter = await navigator.gpu.requestAdapter();
	if (!adapter) {
		err('WebGPU: no adapter; WebGPU is disabled');
		return;
	}

	// The same optional features as the native backend (gfx_api_webgpu.cpp, requestDevice)
	const wantedFeatures = ['texture-compression-bc', 'texture-compression-etc2', 'texture-compression-astc',
		'depth32float-stencil8', 'texture-formats-tier1', 'timestamp-query'];
	const requiredFeatures = wantedFeatures.filter((feature) => adapter.features.has(feature));

	// The full limits of the adapter: terrain needs at least 256 texture array layers
	const requiredLimits = {};
	for (const limit of ['maxTextureDimension2D', 'maxTextureArrayLayers', 'maxBindGroups',
		'maxSampledTexturesPerShaderStage', 'maxSamplersPerShaderStage', 'maxStorageBuffersPerShaderStage',
		'maxUniformBufferBindingSize', 'maxStorageBufferBindingSize', 'maxBufferSize',
		'maxVertexBuffers', 'maxVertexAttributes', 'maxInterStageShaderVariables',
		'maxDynamicUniformBuffersPerPipelineLayout']) {
		if (adapter.limits[limit] !== undefined) {
			requiredLimits[limit] = adapter.limits[limit];
		}
	}

	const device = await adapter.requestDevice({ requiredFeatures: requiredFeatures, requiredLimits: requiredLimits });
	device.addEventListener('uncapturederror', (event) => {
		err('WebGPU error: ' + event.error.message);
	});
	device.lost.then((info) => {
		err('WebGPU device lost (' + info.reason + '): ' + info.message);
	});

	Module['preinitializedWebGPUDevice'] = device;
	// The backend cannot ask the surface for its capabilities without the adapter, so pass the canvas format
	Module['WZVAL_webgpuPreferredCanvasFormat'] = navigator.gpu.getPreferredCanvasFormat();
	const info = adapter.info || {};
	err('WebGPU: device created (' + (info.vendor || '?') + ' ' + (info.architecture || '') + ' ' + (info.description || '') + ')');
}

function wzSetupWebGPUDevice() {
	Module['addRunDependency']('webgpu_device');
	wzCreateWebGPUDevice()
		.catch((error) => {
			err('WebGPU: device creation failed: ' + error);
		})
		.finally(() => {
			Module['removeRunDependency']('webgpu_device');
		});
}

if (!Module['preRun']) {
	Module['preRun'] = [];
}
Module['preRun'].push(wzSetupWebGPUDevice);
