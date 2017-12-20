/*
 * Physically Based Rendering
 * Copyright (c) 2017 Micha³ Siejak
 */

#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "common/mesh.hpp"
#include "common/image.hpp"

#include <d3dx12.h>
#include <d3dcompiler.h>
#include "d3d12.hpp"

namespace D3D12 {

struct TransformCB
{
	glm::mat4 viewProjectionMatrix;
	glm::mat4 skyProjectionMatrix;
	glm::mat4 sceneRotationMatrix;
};

struct ShadingCB
{
	struct {
		glm::vec4 direction;
		glm::vec4 radiance;
	} lights[SceneSettings::NumLights];
	glm::vec4 eyePosition;
};

GLFWwindow* Renderer::initialize(int width, int height, int maxSamples)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, "Physically Based Rendering (Direct3D 12)", nullptr, nullptr);
	if(!window) {
		throw std::runtime_error("Failed to create window");
	}

	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	{
		ComPtr<ID3D12Debug> debugController;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> dxgiFactory;
	if(FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)))) {
		throw std::runtime_error("Failed to create DXGI factory");
	}

	// Find D3D12 compatible adapter and create the device object.
	std::string dxgiAdapterDescription;
	{
		ComPtr<IDXGIAdapter1> adapter = getAdapter(dxgiFactory);
		if(adapter) {
			DXGI_ADAPTER_DESC adapterDesc;
			adapter->GetDesc(&adapterDesc);
			dxgiAdapterDescription = Utility::convertToUTF8(adapterDesc.Description);
		}
		else {
			throw std::runtime_error("No suitable video adapter supporting D3D12 found");
		}

		if(FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
			throw std::runtime_error("Failed to create D3D12 device");
		}

	}

	// Create default direct command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if(FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)))) {
		throw std::runtime_error("Failed to create command queue");
	}

	// Create window swap chain.
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NumFrames;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		ComPtr<IDXGISwapChain1> swapChain;
		if(FAILED(dxgiFactory->CreateSwapChainForHwnd(m_commandQueue.Get(), glfwGetWin32Window(window), &swapChainDesc, nullptr, nullptr, &swapChain))) {
			throw std::runtime_error("Failed to create swap chain");
		}
		swapChain.As(&m_swapChain);
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	dxgiFactory->MakeWindowAssociation(glfwGetWin32Window(window), DXGI_MWA_NO_ALT_ENTER);

	// Determine maximum supported MSAA level.
	UINT samples;
	for(samples = maxSamples; samples > 1; samples /= 2) {
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS mqlColor = { DXGI_FORMAT_R16G16B16A16_FLOAT, samples };
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS mqlDepthStencil = { DXGI_FORMAT_D24_UNORM_S8_UINT, samples };
		m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &mqlColor, sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS));
		m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &mqlDepthStencil, sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS));
		if(mqlColor.NumQualityLevels > 0 && mqlDepthStencil.NumQualityLevels > 0) {
			break;
		}
	}

	// Determine supported root signature version.
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureFeature = {D3D_ROOT_SIGNATURE_VERSION_1_1};
		m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureFeature, sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE));
		m_rootSignatureVersion = rootSignatureFeature.HighestVersion;
	}

	// Create descriptor heaps.
	m_descHeapRTV = createDescriptorHeap({D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
	m_descHeapDSV = createDescriptorHeap({D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
	m_descHeapCBV_SRV_UAV = createDescriptorHeap({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE});

	// Create per-frame resources.
	for(UINT frameIndex=0; frameIndex < NumFrames; ++frameIndex) {

		if(FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[frameIndex])))) {
			throw std::runtime_error("Failed to create command allocator");
		}
		if(FAILED(m_swapChain->GetBuffer(frameIndex, IID_PPV_ARGS(&m_backbuffers[frameIndex].buffer)))) {
			throw std::runtime_error("Failed to retrieve swap chain back buffer");
		}

		m_backbuffers[frameIndex].rtv = m_descHeapRTV.alloc();
		m_device->CreateRenderTargetView(m_backbuffers[frameIndex].buffer.Get(), nullptr, m_backbuffers[frameIndex].rtv.cpuHandle);

		m_framebuffers[frameIndex] = createFrameBuffer(width, height, samples, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT);
		if(samples > 1) {
			m_resolveFramebuffers[frameIndex] = createFrameBuffer(width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, (DXGI_FORMAT)0);
		}
		else {
			m_resolveFramebuffers[frameIndex] = m_framebuffers[frameIndex];
		}
	}

	// Create frame synchronization objects.
	{
		if(FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
			throw std::runtime_error("Failed to create fence object");
		}
		m_fenceCompletionEvent = CreateEvent(nullptr, false, false, nullptr);
	}

	std::printf("Direct3D 12 Renderer [%s]\n", dxgiAdapterDescription.c_str());
	return window;
}

void Renderer::shutdown()
{
	waitForGPU();
	CloseHandle(m_fenceCompletionEvent);
}

void Renderer::setup()
{
	CD3DX12_STATIC_SAMPLER_DESC defaultSamplerDesc{0, D3D12_FILTER_ANISOTROPIC};
	defaultSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	
	CD3DX12_STATIC_SAMPLER_DESC computeSamplerDesc{0, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
	defaultSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_STATIC_SAMPLER_DESC spBRDF_SamplerDesc{1, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
	spBRDF_SamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	spBRDF_SamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	spBRDF_SamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	
	// Create default command list.
	if(FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)))) {
		throw std::runtime_error("Failed to create direct command list");
	}

	// Create root signature & pipeline configuration for tonemapping.
	{
		const std::vector<D3D12_INPUT_ELEMENT_DESC> screenQuadInputLayout = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		ComPtr<ID3DBlob> tonemapVS = compileShader("shaders/hlsl/tonemap.hlsl", "main_vs", "vs_5_0");
		ComPtr<ID3DBlob> tonemapPS = compileShader("shaders/hlsl/tonemap.hlsl", "main_ps", "ps_5_0");

		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc = {};
		signatureDesc.Init_1_1(1, rootParameters, 1, &computeSamplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS);
		m_tonemapRootSignature = createRootSignature(signatureDesc);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_tonemapRootSignature.Get();
		psoDesc.InputLayout = { screenQuadInputLayout.data(), (UINT)screenQuadInputLayout.size() };
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(tonemapVS.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(tonemapPS.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{D3D12_DEFAULT};
		psoDesc.RasterizerState.FrontCounterClockwise = true;
		psoDesc.BlendState = CD3DX12_BLEND_DESC{D3D12_DEFAULT};
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleMask = UINT_MAX;

		if(FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_tonemapPipelineState)))) {
			throw std::runtime_error("Failed to create tonemap pipeline state");
		}
	}

	// Create screen space quad vertex buffer (for tonemapping).
	m_screenQuad = createClipSpaceQuad();

	// Create root signature & pipeline configuration for rendering PBR model.
	{
		const std::vector<D3D12_INPUT_ELEMENT_DESC> meshInputLayout = {
			{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		ComPtr<ID3DBlob> pbrVS = compileShader("shaders/hlsl/pbr.hlsl", "main_vs", "vs_5_0");
		ComPtr<ID3DBlob> pbrPS = compileShader("shaders/hlsl/pbr.hlsl", "main_ps", "ps_5_0");

		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_VERTEX);
		rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		D3D12_STATIC_SAMPLER_DESC staticSamplers[2];
		staticSamplers[0] = defaultSamplerDesc;
		staticSamplers[1] = spBRDF_SamplerDesc;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc;
		signatureDesc.Init_1_1(3, rootParameters, 2, staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		m_pbrRootSignature = createRootSignature(signatureDesc);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pbrRootSignature.Get();
		psoDesc.InputLayout = { meshInputLayout.data(), (UINT)meshInputLayout.size() };
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(pbrVS.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pbrPS.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.FrontCounterClockwise = true;
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = m_framebuffers[0].samples;
		psoDesc.SampleMask = UINT_MAX;

		if(FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pbrPipelineState)))) {
			throw std::runtime_error("Failed to create graphics pipeline state for PBR model");
		}
	}

	// Load PBR model assets.
	m_pbrModel = createMeshBuffer(Mesh::fromFile("meshes/cerberus.fbx"));

	m_albedoTexture = createTexture(Image::fromFile("textures/cerberus_A.png"), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	m_normalTexture = createTexture(Image::fromFile("textures/cerberus_N.png"), DXGI_FORMAT_R8G8B8A8_UNORM);
	m_metalnessTexture = createTexture(Image::fromFile("textures/cerberus_M.png", 1), DXGI_FORMAT_R8_UNORM);
	m_roughnessTexture = createTexture(Image::fromFile("textures/cerberus_R.png", 1), DXGI_FORMAT_R8_UNORM);

	// Create root signature & pipeline configuration for rendering skybox.
	{
		const std::vector<D3D12_INPUT_ELEMENT_DESC> skyboxInputLayout = {
			{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	
		ComPtr<ID3DBlob> skyboxVS = compileShader("shaders/hlsl/skybox.hlsl", "main_vs", "vs_5_0");
		ComPtr<ID3DBlob> skyboxPS = compileShader("shaders/hlsl/skybox.hlsl", "main_ps", "ps_5_0");
		

		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_VERTEX);
		rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc = {};
		signatureDesc.Init_1_1(2, rootParameters, 1, &defaultSamplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		m_skyboxRootSignature = createRootSignature(signatureDesc);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_skyboxRootSignature.Get();
		psoDesc.InputLayout = { skyboxInputLayout.data(), (UINT)skyboxInputLayout.size() };
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(skyboxVS.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(skyboxPS.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.FrontCounterClockwise = true;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = m_framebuffers[0].samples;
		psoDesc.SampleMask = UINT_MAX;

		if(FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_skyboxPipelineState)))) {
			throw std::runtime_error("Failed to create graphics pipeline state for skybox");
		}
	}

	// Load skybox assets.
	m_skybox = createMeshBuffer(Mesh::fromFile("meshes/skybox.obj"));

	// Load & pre-process environment map.
	{
		ComPtr<ID3D12RootSignature> computeRootSignature;

		ID3D12DescriptorHeap* computeDescriptorHeaps[] = {
			m_descHeapCBV_SRV_UAV.heap.Get()
		};

		// Create universal compute root signature.
		{
			const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
				{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
				{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
			};
			CD3DX12_ROOT_PARAMETER1 rootParameters[3];
			rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0]);
			rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1]);
			rootParameters[2].InitAsConstants(1, 0);
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc;
			signatureDesc.Init_1_1(3, rootParameters, 1, &computeSamplerDesc);
			computeRootSignature = createRootSignature(signatureDesc);
		}		

		m_envTexture = createTexture(1024, 1024, 6, DXGI_FORMAT_R16G16B16A16_FLOAT);
		{
			DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);

			// Unfiltered environment cube map (temporary).
			Texture envTextureUnfiltered = createTexture(1024, 1024, 6, DXGI_FORMAT_R16G16B16A16_FLOAT);
			createTextureUAV(envTextureUnfiltered, 0);
			
			// Load & convert equirectangular environment map to cubemap texture
			{
				DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);
				
				Texture envTextureEquirect = createTexture(Image::fromFile("environment.hdr"), DXGI_FORMAT_R32G32B32A32_FLOAT, 1);

				ComPtr<ID3D12PipelineState> pipelineState;
				ComPtr<ID3DBlob> equirectToCubemapShader = compileShader("shaders/hlsl/equirect2cube.hlsl", "main", "cs_5_0");

				D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
				psoDesc.pRootSignature = computeRootSignature.Get();
				psoDesc.CS = CD3DX12_SHADER_BYTECODE(equirectToCubemapShader.Get());
				if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
					throw std::runtime_error("Failed to create compute pipeline state (equirect2cube)");
				}

				m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(envTextureUnfiltered.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
				m_commandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
				m_commandList->SetPipelineState(pipelineState.Get());
				m_commandList->SetComputeRootSignature(computeRootSignature.Get());
				m_commandList->SetComputeRootDescriptorTable(0, envTextureEquirect.srv.gpuHandle);
				m_commandList->SetComputeRootDescriptorTable(1, envTextureUnfiltered.uav.gpuHandle);
				m_commandList->Dispatch(m_envTexture.width/32, m_envTexture.height/32, 6);
				m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(envTextureUnfiltered.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

				// This will implicitly execute command list & wait for GPU to finish.
				generateMipmaps(envTextureUnfiltered);
			}

			// Compute pre-filtered specular environment map.
			{
				DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);

				ComPtr<ID3D12PipelineState> pipelineState;
				ComPtr<ID3DBlob> spmapShader = compileShader("shaders/hlsl/spmap.hlsl", "main", "cs_5_0");

				D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
				psoDesc.pRootSignature = computeRootSignature.Get();
				psoDesc.CS = CD3DX12_SHADER_BYTECODE{spmapShader.Get()};
				if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
					throw std::runtime_error("Failed to create compute pipeline state (spmap)");
				}

				// Copy 0th mipmap level into destination environment map.
				const D3D12_RESOURCE_BARRIER preCopyBarriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_envTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
					CD3DX12_RESOURCE_BARRIER::Transition(envTextureUnfiltered.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
				};
				const D3D12_RESOURCE_BARRIER postCopyBarriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_envTexture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(envTextureUnfiltered.texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
				};
				m_commandList->ResourceBarrier(2, preCopyBarriers);
				for(UINT arraySlice=0; arraySlice<6; ++arraySlice) {
					const UINT subresourceIndex = D3D12CalcSubresource(0, arraySlice, 0, m_envTexture.levels, 6);
					m_commandList->CopyTextureRegion(&CD3DX12_TEXTURE_COPY_LOCATION{m_envTexture.texture.Get(), subresourceIndex}, 0, 0, 0, &CD3DX12_TEXTURE_COPY_LOCATION{envTextureUnfiltered.texture.Get(), subresourceIndex}, nullptr);
				}
				m_commandList->ResourceBarrier(2, postCopyBarriers);

				// Pre-filter rest of the mip chain.
				m_commandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
				m_commandList->SetPipelineState(pipelineState.Get());
				m_commandList->SetComputeRootSignature(computeRootSignature.Get());
				m_commandList->SetComputeRootDescriptorTable(0, envTextureUnfiltered.srv.gpuHandle);

				const float deltaRoughness = 1.0f / glm::max(float(m_envTexture.levels-1), 1.0f);
				for(UINT level=1, size=512; level<m_envTexture.levels; ++level, size/=2) {
					const UINT numGroups = glm::max<UINT>(1, size/32);
					const float spmapRoughness = level * deltaRoughness;

					createTextureUAV(m_envTexture, level);

					m_commandList->SetComputeRootDescriptorTable(1, m_envTexture.uav.gpuHandle);
					m_commandList->SetComputeRoot32BitConstants(2, 1, &spmapRoughness, 0);
					m_commandList->Dispatch(numGroups, numGroups, 6);
				}
				m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_envTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

				executeCommandList();
				waitForGPU();
			}
		}

		// Compute diffuse irradiance cubemap.
		m_irmapTexture = createTexture(32, 32, 6, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
		{
			DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);
			createTextureUAV(m_irmapTexture, 0);

			ComPtr<ID3D12PipelineState> pipelineState;
			ComPtr<ID3DBlob> irmapShader = compileShader("shaders/hlsl/irmap.hlsl", "main", "cs_5_0");

			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = computeRootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE{irmapShader.Get()};
			if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
				throw std::runtime_error("Failed to create compute pipeline state (irmap)");
			}

			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_irmapTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
			m_commandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
			m_commandList->SetPipelineState(pipelineState.Get());
			m_commandList->SetComputeRootSignature(computeRootSignature.Get());
			m_commandList->SetComputeRootDescriptorTable(0, m_envTexture.srv.gpuHandle);
			m_commandList->SetComputeRootDescriptorTable(1, m_irmapTexture.uav.gpuHandle);
			m_commandList->Dispatch(m_irmapTexture.width/32, m_irmapTexture.height/32, 6);
			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_irmapTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
			
			executeCommandList();
			waitForGPU();
		}
	
		// Compute Cook-Torrance BRDF 2D LUT for split-sum approximation.
		m_spBRDF_LUT = createTexture(256, 256, 1, DXGI_FORMAT_R16G16_FLOAT, 1);
		{
			DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);
			createTextureUAV(m_spBRDF_LUT, 0);

			ComPtr<ID3D12PipelineState> pipelineState;
			ComPtr<ID3DBlob> spBRDFShader = compileShader("shaders/hlsl/spbrdf.hlsl", "main", "cs_5_0");

			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = computeRootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE{spBRDFShader.Get()};
			if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
				throw std::runtime_error("Failed to create compute pipeline state (spbrdf)");
			}

			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_spBRDF_LUT.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
			m_commandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
			m_commandList->SetPipelineState(pipelineState.Get());
			m_commandList->SetComputeRootSignature(computeRootSignature.Get());
			m_commandList->SetComputeRootDescriptorTable(1, m_spBRDF_LUT.uav.gpuHandle);
			m_commandList->Dispatch(m_spBRDF_LUT.width/32, m_spBRDF_LUT.height/32, 1);
			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_spBRDF_LUT.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
		
			executeCommandList();
			waitForGPU();
		}
	}

	// Create 64kB host-mapped buffer in the upload heap for shader constants.
	m_constantBuffer = createUploadBuffer(64 * 1024);

	// Configure per-frame resources.
	{
		std::vector<D3D12_RESOURCE_BARRIER> barriers{NumFrames};
		for(UINT frameIndex=0; frameIndex<NumFrames; ++frameIndex) {
			m_transformCBVs[frameIndex] = createConstantBufferView<TransformCB>();
			m_shadingCBVs[frameIndex] = createConstantBufferView<ShadingCB>();

			barriers[frameIndex] = CD3DX12_RESOURCE_BARRIER::Transition(m_resolveFramebuffers[frameIndex].colorTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		m_commandList->ResourceBarrier(NumFrames, barriers.data());
	}

	executeCommandList(false);
	waitForGPU();
}

void Renderer::render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene)
{
	const glm::mat4 projectionMatrix = glm::perspectiveFov(view.fov, float(1024), float(1024), 1.0f, 1000.0f);
	const glm::mat4 viewRotationMatrix = glm::eulerAngleXY(glm::radians(view.pitch), glm::radians(view.yaw));
	const glm::mat4 sceneRotationMatrix = glm::eulerAngleXY(glm::radians(scene.pitch), glm::radians(scene.yaw));
	const glm::mat4 viewMatrix = glm::translate(glm::mat4(), {0.0f, 0.0f, -view.distance}) * viewRotationMatrix;
	const glm::vec3 eyePosition = glm::inverse(viewMatrix)[3];

	const ConstantBufferView& transformCBV = m_transformCBVs[m_frameIndex];
	const ConstantBufferView& shadingCBV = m_shadingCBVs[m_frameIndex];

	// Update transform constant buffer (for vertex shaders).
	{
		TransformCB* transformConstants = transformCBV.as<TransformCB>();
		transformConstants->viewProjectionMatrix = projectionMatrix * viewMatrix;
		transformConstants->skyProjectionMatrix  = projectionMatrix * viewRotationMatrix;
		transformConstants->sceneRotationMatrix  = sceneRotationMatrix;
	}
	
	// Update shading constant buffer (for pixel shader).
	{
		ShadingCB* shadingConstants = shadingCBV.as<ShadingCB>();
		shadingConstants->eyePosition = glm::vec4{eyePosition, 0.0f};
		for(int i=0; i<SceneSettings::NumLights; ++i) {
			const SceneSettings::Light& light = scene.lights[i];
			shadingConstants->lights[i].direction = glm::vec4{light.direction, 0.0f};
			if(light.enabled) {
				shadingConstants->lights[i].radiance = glm::vec4{light.radiance, 0.0f};
			}
			else {
				shadingConstants->lights[i].radiance = glm::vec4{};
			}
		}
	}

	ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_frameIndex].Get();
	const SwapChainBuffer& backbuffer = m_backbuffers[m_frameIndex];
	const FrameBuffer& framebuffer = m_framebuffers[m_frameIndex];
	const FrameBuffer& resolveFramebuffer = m_resolveFramebuffers[m_frameIndex];

	commandAllocator->Reset();
	m_commandList->Reset(commandAllocator, m_skyboxPipelineState.Get());
	
	// Set global state.
	m_commandList->RSSetViewports(1, &CD3DX12_VIEWPORT{0.0f, 0.0f, (FLOAT)framebuffer.width, (FLOAT)framebuffer.height});
	m_commandList->RSSetScissorRects(1, &CD3DX12_RECT{0, 0, (LONG)framebuffer.width, (LONG)framebuffer.height});
	
	ID3D12DescriptorHeap* descriptorHeaps[] = { 
		m_descHeapCBV_SRV_UAV.heap.Get()
	};
	m_commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// If not using MSAA, transition main framebuffer into render target state.
	if(framebuffer.samples <= 1) {
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(framebuffer.colorTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}

	// Prepare for rendering into the main framebuffer.
	m_commandList->OMSetRenderTargets(1, &framebuffer.rtv.cpuHandle, false, &framebuffer.dsv.cpuHandle);
	m_commandList->ClearDepthStencilView(framebuffer.dsv.cpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		
	// Draw skybox.
	{
		m_commandList->SetGraphicsRootSignature(m_skyboxRootSignature.Get());
		m_commandList->SetGraphicsRootDescriptorTable(0, transformCBV.cbv.gpuHandle);
		m_commandList->SetGraphicsRootDescriptorTable(1, m_envTexture.srv.gpuHandle);

		m_commandList->IASetVertexBuffers(0, 1, &m_skybox.vbv);
		m_commandList->IASetIndexBuffer(&m_skybox.ibv);

		m_commandList->DrawIndexedInstanced(m_skybox.numElements, 1, 0, 0, 0);
	}

	// Draw PBR model.
	{
		m_commandList->SetGraphicsRootSignature(m_pbrRootSignature.Get());
		m_commandList->SetGraphicsRootDescriptorTable(0, transformCBV.cbv.gpuHandle);
		m_commandList->SetGraphicsRootDescriptorTable(1, shadingCBV.cbv.gpuHandle);
		m_commandList->SetGraphicsRootDescriptorTable(2, m_albedoTexture.srv.gpuHandle);
		m_commandList->SetPipelineState(m_pbrPipelineState.Get());

		m_commandList->IASetVertexBuffers(0, 1, &m_pbrModel.vbv);
		m_commandList->IASetIndexBuffer(&m_pbrModel.ibv);

		m_commandList->DrawIndexedInstanced(m_pbrModel.numElements, 1, 0, 0, 0);
	}

	// Resolve multisample framebuffer (MSAA) or transition into pixel shader resource state (non-MSAA).
	if(framebuffer.samples > 1) {
		resolveFrameBuffer(framebuffer, resolveFramebuffer, DXGI_FORMAT_R16G16B16A16_FLOAT);
	}
	else {
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(framebuffer.colorTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// Prepare for rendering directly into a back buffer.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backbuffer.buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	m_commandList->OMSetRenderTargets(1, &backbuffer.rtv.cpuHandle, false, nullptr);

	// Draw a full screen quad with tonemapping and gamma correction shader (post-processing).
	{
		m_commandList->SetGraphicsRootSignature(m_tonemapRootSignature.Get());
		m_commandList->SetGraphicsRootDescriptorTable(0, resolveFramebuffer.srv.gpuHandle);
		m_commandList->SetPipelineState(m_tonemapPipelineState.Get());
	
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_commandList->IASetVertexBuffers(0, 1, &m_screenQuad.vbv);

		m_commandList->DrawInstanced(m_screenQuad.numElements, 1, 0, 0);
	}
	
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backbuffer.buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	executeCommandList(false);
	presentFrame();
}

DescriptorHeap Renderer::createDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc) const
{
	DescriptorHeap heap;
	if(FAILED(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.heap)))) {
		throw std::runtime_error("Failed to create descriptor heap");
	}
	heap.numDescriptorsAllocated = 0;
	heap.numDescriptorsInHeap = desc.NumDescriptors;
	heap.descriptorSize = m_device->GetDescriptorHandleIncrementSize(desc.Type);
	return heap;
}
	
MeshBuffer Renderer::createMeshBuffer(const std::shared_ptr<Mesh>& mesh) const
{
	MeshBuffer buffer;
	buffer.numElements = static_cast<UINT>(mesh->faces().size() * 3);
	
	const size_t vertexDataSize = mesh->vertices().size() * sizeof(Mesh::Vertex);
	const size_t indexDataSize = mesh->faces().size() * sizeof(Mesh::Face);

	// Create GPU resources & initialize view structures.

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexDataSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer.vertexBuffer))))
	{
		throw std::runtime_error("Failed to create vertex buffer");
	}
	buffer.vbv.BufferLocation = buffer.vertexBuffer->GetGPUVirtualAddress();
	buffer.vbv.SizeInBytes = static_cast<UINT>(vertexDataSize);
	buffer.vbv.StrideInBytes = sizeof(Mesh::Vertex);

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexDataSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer.indexBuffer))))
	{
		throw std::runtime_error("Failed to create index buffer");
	}
	buffer.ibv.BufferLocation = buffer.indexBuffer->GetGPUVirtualAddress();
	buffer.ibv.SizeInBytes = static_cast<UINT>(indexDataSize);
	buffer.ibv.Format = DXGI_FORMAT_R32_UINT;
	
	// Create and upload to CPU accessible staging buffers.
	StagingBuffer vertexStagingBuffer;
	{
		const D3D12_SUBRESOURCE_DATA data = { mesh->vertices().data() };
		vertexStagingBuffer = createStagingBuffer(buffer.vertexBuffer, 0, 1, &data);
	}
	StagingBuffer indexStagingBuffer;
	{
		const D3D12_SUBRESOURCE_DATA data = { mesh->faces().data() };
		indexStagingBuffer = createStagingBuffer(buffer.indexBuffer, 0, 1, &data);
	}

	// Enqueue upload to the GPU.
	m_commandList->CopyResource(buffer.vertexBuffer.Get(), vertexStagingBuffer.buffer.Get());
	m_commandList->CopyResource(buffer.indexBuffer.Get(), indexStagingBuffer.buffer.Get());

	const D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(buffer.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
		CD3DX12_RESOURCE_BARRIER::Transition(buffer.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
	};
	m_commandList->ResourceBarrier(2, barriers);

	// Execute graphics command list and wait for GPU to finish.
	executeCommandList();
	waitForGPU();

	return buffer;
}

MeshBuffer Renderer::createClipSpaceQuad() const
{
	static const float vertices[] = {
		 1.0f,  1.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 1.0f,
		-1.0f, -1.0f, 0.0f, 1.0f,
	};

	MeshBuffer buffer;
	buffer.numElements = 4;

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices)),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer.vertexBuffer))))
	{
		throw std::runtime_error("Failed to create clip space quad vertex buffer");
	}
	buffer.vbv.BufferLocation = buffer.vertexBuffer->GetGPUVirtualAddress();
	buffer.vbv.SizeInBytes = sizeof(vertices);
	buffer.vbv.StrideInBytes = 4 * sizeof(float);

	StagingBuffer vertexStagingBuffer;
	{
		const D3D12_SUBRESOURCE_DATA data = { vertices };
		vertexStagingBuffer = createStagingBuffer(buffer.vertexBuffer, 0, 1, &data);
	}

	m_commandList->CopyResource(buffer.vertexBuffer.Get(), vertexStagingBuffer.buffer.Get());
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(buffer.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	executeCommandList();
	waitForGPU();

	return buffer;
}
	
UploadBuffer Renderer::createUploadBuffer(UINT capacity) const
{
	UploadBuffer buffer;
	buffer.cursor   = 0;
	buffer.capacity = capacity;

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(capacity),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer.buffer))))
	{
		throw std::runtime_error("Failed to create GPU upload buffer");
	}
	if(FAILED(buffer.buffer->Map(0, &CD3DX12_RANGE{0, 0}, reinterpret_cast<void**>(&buffer.cpuAddress)))) {
		throw std::runtime_error("Failed to map GPU upload buffer to host address space");
	}
	buffer.gpuAddress = buffer.buffer->GetGPUVirtualAddress();
	return buffer;
}
	
UploadBufferRegion Renderer::allocFromUploadBuffer(UploadBuffer& buffer, UINT size, int align) const
{
	const UINT alignedSize = Utility::roundToPowerOfTwo(size, align);
	if(buffer.cursor + alignedSize > buffer.capacity) {
		throw std::overflow_error("Out of upload buffer capacity while allocating memory");
	}

	UploadBufferRegion region;
	region.cpuAddress = reinterpret_cast<void*>(buffer.cpuAddress + buffer.cursor);
	region.gpuAddress = buffer.gpuAddress + buffer.cursor;
	region.size = alignedSize;
	buffer.cursor += alignedSize;
	return region;
}

StagingBuffer Renderer::createStagingBuffer(const ComPtr<ID3D12Resource>& resource, UINT firstSubresource, UINT numSubresources, const D3D12_SUBRESOURCE_DATA* data) const
{
	StagingBuffer stagingBuffer;
	stagingBuffer.firstSubresource = firstSubresource;
	stagingBuffer.numSubresources = numSubresources;
	stagingBuffer.layouts.resize(numSubresources);

	const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

	UINT64 numBytesTotal;
	std::vector<UINT> numRows{numSubresources};
	std::vector<UINT64> rowBytes{numSubresources};
	m_device->GetCopyableFootprints(&resourceDesc, firstSubresource, numSubresources, 0, stagingBuffer.layouts.data(), numRows.data(), rowBytes.data(), &numBytesTotal);

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(numBytesTotal),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&stagingBuffer.buffer))))
	{
		throw std::runtime_error("Failed to create GPU staging buffer");
	}

	if(data) {
		assert(resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D);

		void* bufferMemory;
		if(FAILED(stagingBuffer.buffer->Map(0, &CD3DX12_RANGE{0, 0}, &bufferMemory))) {
			throw std::runtime_error("Failed to map GPU staging buffer to host address space");
		}

		for(UINT subresource=0; subresource<numSubresources; ++subresource) {
			uint8_t* subresourceMemory = reinterpret_cast<uint8_t*>(bufferMemory) + stagingBuffer.layouts[subresource].Offset;
			if(resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				// Generic buffer: Copy everything in one go.
				std::memcpy(subresourceMemory, data->pData, numBytesTotal);
			}
			else {
				// Texture: Copy data line by line.
				for(UINT row=0; row<numRows[subresource]; ++row) {
					const uint8_t* srcRowPtr = reinterpret_cast<const uint8_t*>(data[subresource].pData) + row * data[subresource].RowPitch;
					uint8_t* destRowPtr = subresourceMemory + row * stagingBuffer.layouts[subresource].Footprint.RowPitch;
					std::memcpy(destRowPtr, srcRowPtr, rowBytes[subresource]);
				}
			}
		}
		stagingBuffer.buffer->Unmap(0, nullptr);
	}
	return stagingBuffer;
}
	
Texture Renderer::createTexture(UINT width, UINT height, UINT depth, DXGI_FORMAT format, UINT levels)
{
	assert(depth == 1 || depth == 6);

	Texture texture;
	texture.width = width;
	texture.height = height;
	texture.levels = (levels > 0) ? levels : Utility::numMipmapLevels(width, height);

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width  = width;
	desc.Height = height;
	desc.DepthOrArraySize = depth;
	desc.MipLevels = levels;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if(FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&texture.texture))))
	{
		throw std::runtime_error("Failed to create 2D texture");
	}

	D3D12_SRV_DIMENSION srvDim;
	switch(depth) {
	case 1:  srvDim = D3D12_SRV_DIMENSION_TEXTURE2D; break;
	case 6:  srvDim = D3D12_SRV_DIMENSION_TEXTURECUBE; break;
	default: srvDim = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; break;
	}
	createTextureSRV(texture, srvDim);
	return texture;
}

Texture Renderer::createTexture(const std::shared_ptr<Image>& image, DXGI_FORMAT format, UINT levels)
{
	Texture texture = createTexture(image->width(), image->height(), 1, format, levels);
	StagingBuffer textureStagingBuffer;
	{
		const D3D12_SUBRESOURCE_DATA data{ image->pixels<void>(), image->pitch() };
		textureStagingBuffer = createStagingBuffer(texture.texture, 0, 1, &data);
	}

	{
		const CD3DX12_TEXTURE_COPY_LOCATION destCopyLocation{texture.texture.Get(), 0};
		const CD3DX12_TEXTURE_COPY_LOCATION srcCopyLocation{textureStagingBuffer.buffer.Get(), textureStagingBuffer.layouts[0]};

		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST, 0));
		m_commandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, 0));
	}

	if(texture.levels > 1 && texture.width == texture.height && Utility::isPowerOfTwo(texture.width)) {
		generateMipmaps(texture);
	}
	else {
		// Only execute & wait if not generating mipmaps (so that we don't wait redundantly).
		executeCommandList();
		waitForGPU();
	}
	return texture;
}

void Renderer::generateMipmaps(const Texture& texture)
{
	assert(texture.width == texture.height);
	assert(Utility::isPowerOfTwo(texture.width));

	if(!m_mipmapGeneration.rootSignature) {
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
			{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0]);
		rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1]);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(2, rootParameters);
		m_mipmapGeneration.rootSignature = createRootSignature(rootSignatureDesc);
	}
	
	ID3D12PipelineState* pipelineState = nullptr;
	
	const D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();
	if(desc.DepthOrArraySize == 1 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		if(!m_mipmapGeneration.gammaTexturePipelineState) {
			ComPtr<ID3DBlob> computeShader = compileShader("shaders/hlsl/downsample.hlsl", "downsample_gamma", "cs_5_0");
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = m_mipmapGeneration.rootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
			if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_mipmapGeneration.gammaTexturePipelineState)))) {
				throw std::runtime_error("Failed to create compute pipeline state (gamma correct downsample filter)");
			}
		}
		pipelineState = m_mipmapGeneration.gammaTexturePipelineState.Get();
	}
	else if(desc.DepthOrArraySize > 1 && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		if(!m_mipmapGeneration.arrayTexturePipelineState) {
			ComPtr<ID3DBlob> computeShader = compileShader("shaders/hlsl/downsample_array.hlsl", "downsample_linear", "cs_5_0");
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = m_mipmapGeneration.rootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
			if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_mipmapGeneration.arrayTexturePipelineState)))) {
				throw std::runtime_error("Failed to create compute pipeline state (array downsample filter)");
			}
		}
		pipelineState = m_mipmapGeneration.arrayTexturePipelineState.Get();
	}
	else {
		assert(desc.DepthOrArraySize == 1);
		if(!m_mipmapGeneration.linearTexturePipelineState) {
			ComPtr<ID3DBlob> computeShader = compileShader("shaders/hlsl/downsample.hlsl", "downsample_linear", "cs_5_0");
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = m_mipmapGeneration.rootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
			if(FAILED(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_mipmapGeneration.linearTexturePipelineState)))) {
				throw std::runtime_error("Failed to create compute pipeline state (linear downsample filter)");
			}
		}
		pipelineState = m_mipmapGeneration.gammaTexturePipelineState.Get();
	}

	DescriptorHeapMark mark(m_descHeapCBV_SRV_UAV);

	Texture linearTexture = texture;
	if(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pipelineState = m_mipmapGeneration.gammaTexturePipelineState.Get();
		linearTexture = createTexture(texture.width, texture.height, 1, DXGI_FORMAT_R8G8B8A8_UNORM, texture.levels);

		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(linearTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
		m_commandList->CopyResource(linearTexture.texture.Get(), texture.texture.Get());
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(linearTexture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));
	}

	ID3D12DescriptorHeap* descriptorHeaps[] = { 
		m_descHeapCBV_SRV_UAV.heap.Get()
	};

	m_commandList->SetComputeRootSignature(m_mipmapGeneration.rootSignature.Get());
	m_commandList->SetDescriptorHeaps(1, descriptorHeaps);
	m_commandList->SetPipelineState(pipelineState);

	std::vector<CD3DX12_RESOURCE_BARRIER> preDispatchBarriers{desc.DepthOrArraySize};
	std::vector<CD3DX12_RESOURCE_BARRIER> postDispatchBarriers{desc.DepthOrArraySize};
	for(UINT level=1, levelWidth=texture.width/2, levelHeight=texture.height/2; level<texture.levels; ++level, levelWidth/=2, levelHeight/=2) {
		createTextureSRV(linearTexture, desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D, level-1, 1);
		createTextureUAV(linearTexture, level);

		for(UINT arraySlice=0; arraySlice<desc.DepthOrArraySize; ++arraySlice) {
			const UINT subresourceIndex = D3D12CalcSubresource(level, arraySlice, 0, texture.levels, desc.DepthOrArraySize);
			preDispatchBarriers[arraySlice] = CD3DX12_RESOURCE_BARRIER::Transition(linearTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, subresourceIndex);
			postDispatchBarriers[arraySlice] = CD3DX12_RESOURCE_BARRIER::Transition(linearTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);
		}

		m_commandList->ResourceBarrier(desc.DepthOrArraySize, preDispatchBarriers.data());
		m_commandList->SetComputeRootDescriptorTable(0, linearTexture.srv.gpuHandle);
		m_commandList->SetComputeRootDescriptorTable(1, linearTexture.uav.gpuHandle);
		m_commandList->Dispatch(glm::max(UINT(1), levelWidth/8), glm::max(UINT(1), levelHeight/8), desc.DepthOrArraySize);
		m_commandList->ResourceBarrier(desc.DepthOrArraySize, postDispatchBarriers.data());
	}
	
	if(texture.texture == linearTexture.texture) {
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.texture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
	}
	else {
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		m_commandList->CopyResource(texture.texture.Get(), linearTexture.texture.Get());
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));
	}

	executeCommandList();
	waitForGPU();
}

void Renderer::createTextureSRV(Texture& texture, D3D12_SRV_DIMENSION dimension, UINT mostDetailedMip, UINT mipLevels)
{
	const D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();
	const UINT effectiveMipLevels = (mipLevels > 0) ? mipLevels : (desc.MipLevels - mostDetailedMip);
	assert(!(desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE));

	texture.srv = m_descHeapCBV_SRV_UAV.alloc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = dimension;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	switch(dimension) {
	case D3D12_SRV_DIMENSION_TEXTURE2D:
		srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
		srvDesc.Texture2D.MipLevels = effectiveMipLevels;
		break;
	case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
		srvDesc.Texture2DArray.MostDetailedMip = mostDetailedMip;
		srvDesc.Texture2DArray.MipLevels = effectiveMipLevels;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
		break;
	case D3D12_SRV_DIMENSION_TEXTURECUBE:
		assert(desc.DepthOrArraySize == 6);
		srvDesc.TextureCube.MostDetailedMip = mostDetailedMip;
		srvDesc.TextureCube.MipLevels = effectiveMipLevels;
		break;
	default:
		assert(0);
	}
	m_device->CreateShaderResourceView(texture.texture.Get(), &srvDesc, texture.srv.cpuHandle);
}
	
void Renderer::createTextureUAV(Texture& texture, UINT mipSlice)
{
	const D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();
	assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	texture.uav = m_descHeapCBV_SRV_UAV.alloc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	if(desc.DepthOrArraySize > 1) {
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = mipSlice;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
	}
	else {
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = mipSlice;
	}
	m_device->CreateUnorderedAccessView(texture.texture.Get(), nullptr, &uavDesc, texture.uav.cpuHandle);
}
	
FrameBuffer Renderer::createFrameBuffer(UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat)
{
	FrameBuffer fb = {};
	fb.width   = width;
	fb.height  = height;
	fb.samples = samples;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = samples;

	if(colorFormat != DXGI_FORMAT_UNKNOWN) {
		desc.Format = colorFormat;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		const float optimizedClearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
		if(FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&CD3DX12_CLEAR_VALUE{colorFormat, optimizedClearColor},
			IID_PPV_ARGS(&fb.colorTexture))))
		{
			throw std::runtime_error("Failed to create FrameBuffer color texture");
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = desc.Format;
		rtvDesc.ViewDimension = (samples > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
		
		fb.rtv = m_descHeapRTV.alloc();
		m_device->CreateRenderTargetView(fb.colorTexture.Get(), &rtvDesc, fb.rtv.cpuHandle);

		if(samples <= 1) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			
			fb.srv = m_descHeapCBV_SRV_UAV.alloc();
			m_device->CreateShaderResourceView(fb.colorTexture.Get(), &srvDesc, fb.srv.cpuHandle);
		}
	}

	if(depthstencilFormat != DXGI_FORMAT_UNKNOWN) {
		desc.Format = depthstencilFormat;
		desc.Flags  = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

		if(FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE{depthstencilFormat, 1.0f, 0},
			IID_PPV_ARGS(&fb.depthStencilTexture))))
		{
			throw std::runtime_error("Failed to create FrameBuffer depth-stencil texture");
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = desc.Format;
		dsvDesc.ViewDimension = (samples > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;

		fb.dsv = m_descHeapDSV.alloc();
		m_device->CreateDepthStencilView(fb.depthStencilTexture.Get(), &dsvDesc, fb.dsv.cpuHandle);
	}
	return fb;
}
	
void Renderer::resolveFrameBuffer(const FrameBuffer& srcfb, const FrameBuffer& dstfb, DXGI_FORMAT format) const
{
	const CD3DX12_RESOURCE_BARRIER preResolveBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(srcfb.colorTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(dstfb.colorTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
	};
	const CD3DX12_RESOURCE_BARRIER postResolveBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(srcfb.colorTexture.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(dstfb.colorTexture.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};

	if(srcfb.colorTexture != dstfb.colorTexture) {
		m_commandList->ResourceBarrier(2, preResolveBarriers);
		m_commandList->ResolveSubresource(dstfb.colorTexture.Get(), 0, srcfb.colorTexture.Get(), 0, format);
		m_commandList->ResourceBarrier(2, postResolveBarriers);
	}
}
	
ComPtr<ID3D12RootSignature> Renderer::createRootSignature(D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc) const
{
	const D3D12_ROOT_SIGNATURE_FLAGS standardFlags = 
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

	switch(desc.Version) {
	case D3D_ROOT_SIGNATURE_VERSION_1_0:
		desc.Desc_1_0.Flags |= standardFlags;
		break;
	case D3D_ROOT_SIGNATURE_VERSION_1_1:
		desc.Desc_1_1.Flags |= standardFlags;
		break;
	}

	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	if(FAILED(D3DX12SerializeVersionedRootSignature(&desc, m_rootSignatureVersion, &signatureBlob, &errorBlob))) {
		throw std::runtime_error("Failed to serialize root signature");
	}
	if(FAILED(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
		throw std::runtime_error("Failed to create root signature");
	}
	return rootSignature;
}
		
ConstantBufferView Renderer::createConstantBufferView(const void* data, UINT size)
{
	ConstantBufferView view;
	view.data = allocFromUploadBuffer(m_constantBuffer, size, 256);
	view.cbv = m_descHeapCBV_SRV_UAV.alloc();
	if(data) {
		std::memcpy(view.data.cpuAddress, data, size);
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
	desc.BufferLocation = view.data.gpuAddress;
	desc.SizeInBytes = view.data.size;
	m_device->CreateConstantBufferView(&desc, view.cbv.cpuHandle);

	return view;
}

void Renderer::executeCommandList(bool reset) const
{
	if(FAILED(m_commandList->Close())) {
		throw std::runtime_error("Failed close command list (validation error or not in recording state)");
	}

	ID3D12CommandList* lists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, lists);

	if(reset) {
		m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);
	}
}

void Renderer::waitForGPU() const
{
	UINT64& fenceValue = m_fenceValues[m_frameIndex];
	m_commandQueue->Signal(m_fence.Get(), fenceValue);
	m_fence->SetEventOnCompletion(fenceValue, m_fenceCompletionEvent);
	WaitForSingleObject(m_fenceCompletionEvent, INFINITE);
	++fenceValue;
}
	
void Renderer::presentFrame()
{
	m_swapChain->Present(1, 0);
	
	const UINT64 prevFrameFenceValue = m_fenceValues[m_frameIndex];
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	UINT64& currentFrameFenceValue = m_fenceValues[m_frameIndex];

	m_commandQueue->Signal(m_fence.Get(), prevFrameFenceValue);

	if(m_fence->GetCompletedValue() < currentFrameFenceValue) {
		m_fence->SetEventOnCompletion(currentFrameFenceValue, m_fenceCompletionEvent);
		WaitForSingleObject(m_fenceCompletionEvent, INFINITE);
	}
	currentFrameFenceValue = prevFrameFenceValue + 1;
}
	
ComPtr<IDXGIAdapter1> Renderer::getAdapter(const ComPtr<IDXGIFactory4>& factory)
{
	ComPtr<IDXGIAdapter1> adapter;
	for(UINT index=0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}
		if(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
			return adapter;
		}
	}
	return nullptr;
}
	
ComPtr<ID3DBlob> Renderer::compileShader(const std::string& filename, const std::string& entryPoint, const std::string& profile)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if _DEBUG
	flags |= D3DCOMPILE_DEBUG;
	flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3DBlob> shader;
	ComPtr<ID3DBlob> errorBlob;

	std::printf("Compiling HLSL shader: %s [%s]\n", filename.c_str(), entryPoint.c_str());

	if(FAILED(D3DCompileFromFile(Utility::convertToUTF16(filename).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), profile.c_str(), flags, 0, &shader, &errorBlob))) {
		std::string errorMsg = "Shader compilation failed: " + filename;
		if(errorBlob) {
			errorMsg += std::string("\n") + static_cast<const char*>(errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(errorMsg);
	}
	return shader;
}

} // D3D12
