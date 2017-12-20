/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "common/mesh.hpp"
#include "common/image.hpp"

#include "d3d11.hpp"
#include <dxgi.h>
#include <d3dcompiler.h>

namespace D3D11 {

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
	GLFWwindow* window = glfwCreateWindow(width, height, "Physically Based Rendering (Direct3D 11)", nullptr, nullptr);
	if(!window) {
		throw std::runtime_error("Failed to create window");
	}
	
	UINT deviceFlags = 0;
#if _DEBUG
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, &featureLevel, 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_context))) {
		throw std::runtime_error("Failed to create D3D11 device");
	}

	ComPtr<IDXGIFactory1> dxgiFactory;
	std::string dxgiAdapterDescription;
	{
		ComPtr<IDXGIDevice> dxgiDevice;
		if(SUCCEEDED(m_device.As<IDXGIDevice>(&dxgiDevice))) {
			ComPtr<IDXGIAdapter> dxgiAdapter;
			if(SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
				DXGI_ADAPTER_DESC adapterDesc;
				dxgiAdapter->GetDesc(&adapterDesc);
				dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
				dxgiAdapterDescription = Utility::convertToUTF8(adapterDesc.Description);
			}
		}
	}
	if(!dxgiFactory) {
		throw std::runtime_error("Failed to retrieve the IDXGIFactory1 interface associated with D3D11 device");
	}

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.OutputWindow = glfwGetWin32Window(window);
	swapChainDesc.Windowed = true;

	if(FAILED(dxgiFactory->CreateSwapChain(m_device.Get(), &swapChainDesc, &m_swapChain))) {
		throw std::runtime_error("Failed to create the swap chain");
	}
	dxgiFactory->MakeWindowAssociation(glfwGetWin32Window(window), DXGI_MWA_NO_ALT_ENTER);

	{
		ComPtr<ID3D11Texture2D> backBuffer;
		m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if(FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferRTV))) {
			throw std::runtime_error("Failed to create window back buffer render target view");
		}
	}

	// Determine maximum supported MSAA level.
	UINT samples;
	for(samples = maxSamples; samples > 1; samples /= 2) {
		UINT colorQualityLevels;
		UINT depthStencilQualityLevels;
		m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, samples, &colorQualityLevels);
		m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, samples, &depthStencilQualityLevels);
		if(colorQualityLevels > 0 && depthStencilQualityLevels > 0) {
			break;
		}
	}
	
	m_framebuffer = createFrameBuffer(width, height, samples, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT);
	if(samples > 1) {
		m_resolveFramebuffer = createFrameBuffer(width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, (DXGI_FORMAT)0);
	}
	else {
		m_resolveFramebuffer = m_framebuffer;
	}

	D3D11_VIEWPORT viewport = {};
	viewport.Width    = (FLOAT)width;
	viewport.Height   = (FLOAT)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &viewport);

	std::printf("Direct3D 11 Renderer [%s]\n", dxgiAdapterDescription.c_str());
	return window;
}
	
void Renderer::setup() 
{
	const std::vector<D3D11_INPUT_ELEMENT_DESC> meshInputLayout = {
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	const std::vector<D3D11_INPUT_ELEMENT_DESC> skyboxInputLayout = {
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	const std::vector<D3D11_INPUT_ELEMENT_DESC> screenQuadInputLayout = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	ID3D11UnorderedAccessView* const nullUAV[] = { nullptr };
	ID3D11Buffer* const nullBuffer[] = { nullptr };

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.FrontCounterClockwise = true;
	rasterizerDesc.DepthClipEnable = true;
	if(FAILED(m_device->CreateRasterizerState(&rasterizerDesc, &m_defaultRasterizerState))) {
		throw std::runtime_error("Failed to create default rasterizer state");
	}

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
	if(FAILED(m_device->CreateDepthStencilState(&depthStencilDesc, &m_defaultDepthStencilState))) {
		throw std::runtime_error("Failed to create default depth-stencil state");
	}

	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	if(FAILED(m_device->CreateDepthStencilState(&depthStencilDesc, &m_skyboxDepthStencilState))) {
		throw std::runtime_error("Failed to create skybox depth-stencil state");
	}

	m_defaultSampler = createSamplerState(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);
	m_computeSampler = createSamplerState(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);

	m_transformCB = createConstantBuffer<TransformCB>();
	m_shadingCB = createConstantBuffer<ShadingCB>();

	m_pbrProgram = createShaderProgram(
		compileShader("shaders/hlsl/pbr.hlsl", "main_vs", "vs_5_0"),
		compileShader("shaders/hlsl/pbr.hlsl", "main_ps", "ps_5_0"),
		meshInputLayout
	);
	m_skyboxProgram = createShaderProgram(
		compileShader("shaders/hlsl/skybox.hlsl", "main_vs", "vs_5_0"),
		compileShader("shaders/hlsl/skybox.hlsl", "main_ps", "ps_5_0"),
		skyboxInputLayout
	);
	m_tonemapProgram = createShaderProgram(
		compileShader("shaders/hlsl/tonemap.hlsl", "main_vs", "vs_5_0"),
		compileShader("shaders/hlsl/tonemap.hlsl", "main_ps", "ps_5_0"),
		screenQuadInputLayout
	);

	m_screenQuad = createClipSpaceQuad();

	m_pbrModel = createMeshBuffer(Mesh::fromFile("meshes/cerberus.fbx"));
	m_skybox = createMeshBuffer(Mesh::fromFile("meshes/skybox.obj"));

	m_albedoTexture = createTexture(Image::fromFile("textures/cerberus_A.png"), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	m_normalTexture = createTexture(Image::fromFile("textures/cerberus_N.png"), DXGI_FORMAT_R8G8B8A8_UNORM);
	m_metalnessTexture = createTexture(Image::fromFile("textures/cerberus_M.png", 1), DXGI_FORMAT_R8_UNORM);
	m_roughnessTexture = createTexture(Image::fromFile("textures/cerberus_R.png", 1), DXGI_FORMAT_R8_UNORM);

	{
		// Unfiltered environment cube map (temporary).
		Texture envTextureUnfiltered = createTextureCube(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT);
		createTextureUAV(envTextureUnfiltered, 0);

		// Load & convert equirectangular environment map to a cubemap texture.
		{
			ComputeProgram equirectToCubeProgram = createComputeProgram(compileShader("shaders/hlsl/equirect2cube.hlsl", "main", "cs_5_0"));
			Texture envTextureEquirect = createTexture(Image::fromFile("environment.hdr"), DXGI_FORMAT_R32G32B32A32_FLOAT, 1);

			m_context->CSSetShaderResources(0, 1, envTextureEquirect.srv.GetAddressOf());
			m_context->CSSetUnorderedAccessViews(0, 1, envTextureUnfiltered.uav.GetAddressOf(), nullptr);
			m_context->CSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
			m_context->CSSetShader(equirectToCubeProgram.computeShader.Get(), nullptr, 0);
			m_context->Dispatch(envTextureUnfiltered.width/32, envTextureUnfiltered.height/32, 6);
			m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		}

		m_context->GenerateMips(envTextureUnfiltered.srv.Get());

		// Compute pre-filtered specular environment map.
		{
			struct SpecularMapFilterSettingsCB
			{
				float roughness;
				float padding[3];
			};
			ComputeProgram spmapProgram = createComputeProgram(compileShader("shaders/hlsl/spmap.hlsl", "main", "cs_5_0"));
			ComPtr<ID3D11Buffer> spmapCB = createConstantBuffer<SpecularMapFilterSettingsCB>();
		
			m_envTexture = createTextureCube(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT);

			// Copy 0th mipmap level into destination environment map.
			for(int arraySlice=0; arraySlice<6; ++arraySlice) {
				const UINT subresourceIndex = D3D11CalcSubresource(0, arraySlice, m_envTexture.levels);
				m_context->CopySubresourceRegion(m_envTexture.texture.Get(), subresourceIndex, 0, 0, 0, envTextureUnfiltered.texture.Get(), subresourceIndex, nullptr);
			}

			m_context->CSSetShaderResources(0, 1, envTextureUnfiltered.srv.GetAddressOf());
			m_context->CSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
			m_context->CSSetShader(spmapProgram.computeShader.Get(), nullptr, 0);

			// Pre-filter rest of the mip chain.
			const float deltaRoughness = 1.0f / glm::max(float(m_envTexture.levels-1), 1.0f);
			for(UINT level=1, size=512; level<m_envTexture.levels; ++level, size/=2) {
				const UINT numGroups = glm::max<UINT>(1, size/32);
				createTextureUAV(m_envTexture, level);

				const SpecularMapFilterSettingsCB spmapConstants = { level * deltaRoughness };
				m_context->UpdateSubresource(spmapCB.Get(), 0, nullptr, &spmapConstants, 0, 0);

				m_context->CSSetConstantBuffers(0, 1, spmapCB.GetAddressOf());
				m_context->CSSetUnorderedAccessViews(0, 1, m_envTexture.uav.GetAddressOf(), nullptr);
				m_context->Dispatch(numGroups, numGroups, 6);
			}
			m_context->CSSetConstantBuffers(0, 1, nullBuffer);
			m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		}
	}

	// Compute diffuse irradiance cubemap.
	{
		ComputeProgram irmapProgram = createComputeProgram(compileShader("shaders/hlsl/irmap.hlsl", "main", "cs_5_0"));
		
		m_irmapTexture = createTextureCube(32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
		createTextureUAV(m_irmapTexture, 0);

		m_context->CSSetShaderResources(0, 1, m_envTexture.srv.GetAddressOf());
		m_context->CSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
		m_context->CSSetUnorderedAccessViews(0, 1, m_irmapTexture.uav.GetAddressOf(), nullptr);
		m_context->CSSetShader(irmapProgram.computeShader.Get(), nullptr, 0);
		m_context->Dispatch(m_irmapTexture.width/32, m_irmapTexture.height/32, 6);
		m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	}

	// Compute Cook-Torrance BRDF 2D LUT for split-sum approximation.
	{
		ComputeProgram spBRDFProgram = createComputeProgram(compileShader("shaders/hlsl/spbrdf.hlsl", "main", "cs_5_0"));

		m_spBRDF_LUT = createTexture(256, 256, DXGI_FORMAT_R16G16_FLOAT, 1);
		m_spBRDF_Sampler = createSamplerState(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
		createTextureUAV(m_spBRDF_LUT, 0);

		m_context->CSSetUnorderedAccessViews(0, 1, m_spBRDF_LUT.uav.GetAddressOf(), nullptr);
		m_context->CSSetShader(spBRDFProgram.computeShader.Get(), nullptr, 0);
		m_context->Dispatch(m_spBRDF_LUT.width/32, m_spBRDF_LUT.height/32, 1);
		m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	}
}
	
void Renderer::render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene)
{
	const glm::mat4 projectionMatrix = glm::perspectiveFov(view.fov, float(m_framebuffer.width), float(m_framebuffer.height), 1.0f, 1000.0f);
	const glm::mat4 viewRotationMatrix = glm::eulerAngleXY(glm::radians(view.pitch), glm::radians(view.yaw));
	const glm::mat4 sceneRotationMatrix = glm::eulerAngleXY(glm::radians(scene.pitch), glm::radians(scene.yaw));
	const glm::mat4 viewMatrix = glm::translate(glm::mat4(), {0.0f, 0.0f, -view.distance}) * viewRotationMatrix;
	const glm::vec3 eyePosition = glm::inverse(viewMatrix)[3];

	// Update transform constant buffer (for vertex shaders).
	{
		TransformCB transformConstants;
		transformConstants.viewProjectionMatrix = projectionMatrix * viewMatrix;
		transformConstants.skyProjectionMatrix  = projectionMatrix * viewRotationMatrix;
		transformConstants.sceneRotationMatrix  = sceneRotationMatrix;
		m_context->UpdateSubresource(m_transformCB.Get(), 0, nullptr, &transformConstants, 0, 0);
	}

	// Update shading constant buffer (for pixel shader).
	{
		ShadingCB shadingConstants;
		shadingConstants.eyePosition = glm::vec4{eyePosition, 0.0f};
		for(int i=0; i<SceneSettings::NumLights; ++i) {
			const SceneSettings::Light& light = scene.lights[i];
			shadingConstants.lights[i].direction = glm::vec4{light.direction, 0.0f};
			if(light.enabled) {
				shadingConstants.lights[i].radiance = glm::vec4{light.radiance, 0.0f};
			}
			else {
				shadingConstants.lights[i].radiance = glm::vec4{};
			}
		}
		m_context->UpdateSubresource(m_shadingCB.Get(), 0, nullptr, &shadingConstants, 0, 0);
	}

	// Prepare framebuffer for rendering.
	m_context->OMSetRenderTargets(1, m_framebuffer.rtv.GetAddressOf(), m_framebuffer.dsv.Get());
	m_context->ClearDepthStencilView(m_framebuffer.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	
	// Set known pipeline state.
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->RSSetState(m_defaultRasterizerState.Get());
	m_context->VSSetConstantBuffers(0, 1, m_transformCB.GetAddressOf());
	m_context->PSSetConstantBuffers(0, 1, m_shadingCB.GetAddressOf());

	// Draw skybox.
	m_context->IASetInputLayout(m_skyboxProgram.inputLayout.Get());
	m_context->IASetVertexBuffers(0, 1, m_skybox.vertexBuffer.GetAddressOf(), &m_skybox.stride, &m_skybox.offset);
	m_context->IASetIndexBuffer(m_skybox.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_context->VSSetShader(m_skyboxProgram.vertexShader.Get(), nullptr, 0);
	m_context->PSSetShader(m_skyboxProgram.pixelShader.Get(), nullptr, 0);
	m_context->PSSetShaderResources(0, 1, m_envTexture.srv.GetAddressOf());
	m_context->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());
	m_context->OMSetDepthStencilState(m_skyboxDepthStencilState.Get(), 0);
	m_context->DrawIndexed(m_skybox.numElements, 0, 0);

	// Draw PBR model.
	ID3D11ShaderResourceView* const pbrModelSRVs[] = {
		m_albedoTexture.srv.Get(),
		m_normalTexture.srv.Get(),
		m_metalnessTexture.srv.Get(),
		m_roughnessTexture.srv.Get(),
		m_envTexture.srv.Get(),
		m_irmapTexture.srv.Get(),
		m_spBRDF_LUT.srv.Get(),
	};
	ID3D11SamplerState* const pbrModelSamplers[] = {
		m_defaultSampler.Get(),
		m_spBRDF_Sampler.Get(),
	};

	m_context->IASetInputLayout(m_pbrProgram.inputLayout.Get());
	m_context->IASetVertexBuffers(0, 1, m_pbrModel.vertexBuffer.GetAddressOf(), &m_pbrModel.stride, &m_pbrModel.offset);
	m_context->IASetIndexBuffer(m_pbrModel.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_context->VSSetShader(m_pbrProgram.vertexShader.Get(), nullptr, 0);
	m_context->PSSetShader(m_pbrProgram.pixelShader.Get(), nullptr, 0);
	m_context->PSSetShaderResources(0, 7, pbrModelSRVs);
	m_context->PSSetSamplers(0, 2, pbrModelSamplers);
	m_context->OMSetDepthStencilState(m_defaultDepthStencilState.Get(), 0);
	m_context->DrawIndexed(m_pbrModel.numElements, 0, 0);

	// Resolve multisample framebuffer.
	resolveFrameBuffer(m_framebuffer, m_resolveFramebuffer, DXGI_FORMAT_R16G16B16A16_FLOAT);

	// Draw a full screen quad with tonemapping and gamma correction shader (post-processing).
	m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), nullptr);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_context->IASetInputLayout(m_tonemapProgram.inputLayout.Get());
	m_context->IASetVertexBuffers(0, 1, m_screenQuad.vertexBuffer.GetAddressOf(), &m_screenQuad.stride, &m_screenQuad.offset);
	m_context->VSSetShader(m_tonemapProgram.vertexShader.Get(), nullptr, 0);
	m_context->PSSetShader(m_tonemapProgram.pixelShader.Get(), nullptr, 0);
	m_context->PSSetShaderResources(0, 1, m_resolveFramebuffer.srv.GetAddressOf());
	m_context->PSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
	m_context->Draw(m_screenQuad.numElements, 0);

	m_swapChain->Present(1, 0);
}
	
MeshBuffer Renderer::createMeshBuffer(const std::shared_ptr<class Mesh>& mesh) const
{
	MeshBuffer buffer = {};
	buffer.stride = sizeof(Mesh::Vertex);
	buffer.numElements = static_cast<UINT>(mesh->faces().size() * 3);

	const size_t vertexDataSize = mesh->vertices().size() * sizeof(Mesh::Vertex);
	const size_t indexDataSize = mesh->faces().size() * sizeof(Mesh::Face);

	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)vertexDataSize;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = &mesh->vertices()[0];
		if(FAILED(m_device->CreateBuffer(&desc, &data, &buffer.vertexBuffer))) {
			throw std::runtime_error("Failed to create vertex buffer");
		}
	}
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)indexDataSize;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = &mesh->faces()[0];
		if(FAILED(m_device->CreateBuffer(&desc, &data, &buffer.indexBuffer))) {
			throw std::runtime_error("Failed to create index buffer");
		}
	}
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
	
	MeshBuffer buffer = {};
	buffer.stride = 4 * sizeof(float);
	buffer.numElements = 4;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(vertices);
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = vertices;
	if(FAILED(m_device->CreateBuffer(&desc, &data, &buffer.vertexBuffer))) {
		throw std::runtime_error("Failed to create clip space quad vertex buffer");
	}
	return buffer;
}
	
Texture Renderer::createTexture(UINT width, UINT height, DXGI_FORMAT format, UINT levels) const
{
	Texture texture;
	texture.width  = width;
	texture.height = height;
	texture.levels = (levels > 0) ? levels : Utility::numMipmapLevels(width, height);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width  = width;
	desc.Height = height;
	desc.MipLevels = levels;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	if(levels == 0) {
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags  = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	}

	if(FAILED(m_device->CreateTexture2D(&desc, nullptr, &texture.texture))) {
		throw std::runtime_error("Failed to create 2D texture");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	if(FAILED(m_device->CreateShaderResourceView(texture.texture.Get(), &srvDesc, &texture.srv))) {
		throw std::runtime_error("Failed to create 2D texture SRV");
	}
	return texture;
}

Texture Renderer::createTextureCube(UINT width, UINT height, DXGI_FORMAT format, UINT levels) const
{
	Texture texture;
	texture.width  = width;
	texture.height = height;
	texture.levels = (levels > 0) ? levels : Utility::numMipmapLevels(width, height);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width  = width;
	desc.Height = height;
	desc.MipLevels = levels;
	desc.ArraySize = 6;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	if(levels == 0) {
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
	}

	if(FAILED(m_device->CreateTexture2D(&desc, nullptr, &texture.texture))) {
		throw std::runtime_error("Failed to create cubemap texture");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = -1;
	if(FAILED(m_device->CreateShaderResourceView(texture.texture.Get(), &srvDesc, &texture.srv))) {
		throw std::runtime_error("Failed to create cubemap texture SRV");
	}
	return texture;
}
	
Texture Renderer::createTexture(const std::shared_ptr<Image>& image, DXGI_FORMAT format, UINT levels) const
{
	Texture texture = createTexture(image->width(), image->height(), format, levels);
	m_context->UpdateSubresource(texture.texture.Get(), 0, nullptr, image->pixels<void>(), image->pitch(), 0);
	if(levels == 0) {
		m_context->GenerateMips(texture.srv.Get());
	}
	return texture;
}
	
void Renderer::createTextureUAV(Texture& texture, UINT mipSlice) const
{
	assert(texture.texture);

	D3D11_TEXTURE2D_DESC desc;
	texture.texture->GetDesc(&desc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	if(desc.ArraySize == 1) {
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = mipSlice;
	}
	else {
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = mipSlice;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = desc.ArraySize;
	}

	if(FAILED(m_device->CreateUnorderedAccessView(texture.texture.Get(), &uavDesc, &texture.uav))) {
		throw std::runtime_error("Failed to create texture UAV");
	}
}
	
ComPtr<ID3D11SamplerState> Renderer::createSamplerState(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode) const
{
	D3D11_SAMPLER_DESC desc = {};
	desc.Filter = filter;
	desc.AddressU = addressMode;
	desc.AddressV = addressMode;
	desc.AddressW = addressMode;
	desc.MaxAnisotropy = (filter == D3D11_FILTER_ANISOTROPIC) ? D3D11_REQ_MAXANISOTROPY : 1;
	desc.MinLOD = 0;
	desc.MaxLOD = D3D11_FLOAT32_MAX;

	ComPtr<ID3D11SamplerState> samplerState;
	if(FAILED(m_device->CreateSamplerState(&desc, &samplerState))) {
		throw std::runtime_error("Failed to create sampler state");
	}
	return samplerState;
}

ShaderProgram Renderer::createShaderProgram(const ComPtr<ID3DBlob>& vsBytecode, const ComPtr<ID3DBlob>& psBytecode, const std::vector<D3D11_INPUT_ELEMENT_DESC>& inputLayoutDesc) const
{
	ShaderProgram program;
	if(FAILED(m_device->CreateVertexShader(vsBytecode->GetBufferPointer(), vsBytecode->GetBufferSize(), nullptr, &program.vertexShader))) {
		throw std::runtime_error("Failed to create vertex shader from compiled bytecode");
	}
	if(FAILED(m_device->CreatePixelShader(psBytecode->GetBufferPointer(), psBytecode->GetBufferSize(), nullptr, &program.pixelShader))) {
		throw std::runtime_error("Failed to create pixel shader from compiled bytecode");
	}
	if(FAILED(m_device->CreateInputLayout(inputLayoutDesc.data(), (UINT)inputLayoutDesc.size(), vsBytecode->GetBufferPointer(), vsBytecode->GetBufferSize(), &program.inputLayout))) {
		throw std::runtime_error("Failed to create shader program input layout");
	}
	return program;
}
	
ComputeProgram Renderer::createComputeProgram(const ComPtr<ID3DBlob>& csBytecode) const
{
	ComputeProgram program;
	if(FAILED(m_device->CreateComputeShader(csBytecode->GetBufferPointer(), csBytecode->GetBufferSize(), nullptr, &program.computeShader))) {
		throw std::runtime_error("Failed to create compute shader from compiled bytecode");
	}
	return program;
}

FrameBuffer Renderer::createFrameBuffer(UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat) const
{
	FrameBuffer fb;
	fb.width   = width;
	fb.height  = height;
	fb.samples = samples;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = samples;

	if(colorFormat != DXGI_FORMAT_UNKNOWN) {
		desc.Format = colorFormat;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		if(samples <= 1) {
			desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		}
		if(FAILED(m_device->CreateTexture2D(&desc, nullptr, &fb.colorTexture))) {
			throw std::runtime_error("Failed to create FrameBuffer color texture");
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = desc.Format;
		rtvDesc.ViewDimension = (samples > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
		if(FAILED(m_device->CreateRenderTargetView(fb.colorTexture.Get(), &rtvDesc, &fb.rtv))) {
			throw std::runtime_error("Failed to create FrameBuffer render target view");
		}

		if(samples <= 1) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			if(FAILED(m_device->CreateShaderResourceView(fb.colorTexture.Get(), &srvDesc, &fb.srv))) {
				throw std::runtime_error("Failed to create FrameBuffer shader resource view");
			}
		}
	}

	if(depthstencilFormat != DXGI_FORMAT_UNKNOWN) {
		desc.Format = depthstencilFormat;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if(FAILED(m_device->CreateTexture2D(&desc, nullptr, &fb.depthStencilTexture))) {
			throw std::runtime_error("Failed to create FrameBuffer depth-stencil texture");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = desc.Format;
		dsvDesc.ViewDimension = (samples > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(m_device->CreateDepthStencilView(fb.depthStencilTexture.Get(), &dsvDesc, &fb.dsv))) {
			throw std::runtime_error("Failed to create FrameBuffer depth-stencil view");
		}
	}

	return fb;
}
	
void Renderer::resolveFrameBuffer(const FrameBuffer& srcfb, const FrameBuffer& dstfb, DXGI_FORMAT format) const
{
	if(srcfb.colorTexture != dstfb.colorTexture) {
		m_context->ResolveSubresource(dstfb.colorTexture.Get(), 0, srcfb.colorTexture.Get(), 0, format);
	}
}

ComPtr<ID3D11Buffer> Renderer::createConstantBuffer(const void* data, UINT size) const
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = static_cast<UINT>(size);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA bufferData = {};
	bufferData.pSysMem = data;

	ComPtr<ID3D11Buffer> buffer;
	const D3D11_SUBRESOURCE_DATA* bufferDataPtr = data ? &bufferData : nullptr;
	if(FAILED(m_device->CreateBuffer(&desc, bufferDataPtr, &buffer))) {
		throw std::runtime_error("Failed to create constant buffer");
	}
	return buffer;
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
	
} // D3D11
