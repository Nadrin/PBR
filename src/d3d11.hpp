/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 *
 * Direct3D 11 renderer.
 */

#pragma once

#if defined(ENABLE_D3D11)

#if !defined(_WIN32)
#error "D3D11 renderer can only be enabled on Windows platform"
#endif

#include <vector>
#include <memory>
#include <d3d11.h>
#include <wrl/client.h>

#include "common/renderer.hpp"
#include "common/utils.hpp"

namespace D3D11 {

using Microsoft::WRL::ComPtr;

struct MeshBuffer
{
	ComPtr<ID3D11Buffer> vertexBuffer;
	ComPtr<ID3D11Buffer> indexBuffer;
	UINT stride;
	UINT offset;
	UINT numElements;
};

struct FrameBuffer
{
	ComPtr<ID3D11Texture2D> colorTexture;
	ComPtr<ID3D11Texture2D> depthStencilTexture;
	ComPtr<ID3D11RenderTargetView> rtv;
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11DepthStencilView> dsv;
	UINT width, height;
	UINT samples;
};

struct ShaderProgram
{
	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader> pixelShader;
	ComPtr<ID3D11InputLayout> inputLayout;
};

struct ComputeProgram
{
	ComPtr<ID3D11ComputeShader> computeShader;
};

struct Texture
{
	ComPtr<ID3D11Texture2D> texture;
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	UINT width, height;
	UINT levels;
};

class Renderer final : public RendererInterface
{
public:
	GLFWwindow* initialize(int width, int height, int maxSamples) override;
	void shutdown() override {}
	void setup() override;
	void render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene) override;

private:
	MeshBuffer createMeshBuffer(const std::shared_ptr<class Mesh>& mesh) const;
	ShaderProgram createShaderProgram(const ComPtr<ID3DBlob>& vsBytecode, const ComPtr<ID3DBlob>& psBytecode, const std::vector<D3D11_INPUT_ELEMENT_DESC>* inputLayoutDesc) const;
	ComputeProgram createComputeProgram(const ComPtr<ID3DBlob>& csBytecode) const;
	ComPtr<ID3D11SamplerState> createSamplerState(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode) const;

	Texture createTexture(UINT width, UINT height, DXGI_FORMAT format, UINT levels=0) const;
	Texture createTexture(const std::shared_ptr<class Image>& image, DXGI_FORMAT format, UINT levels=0) const;
	Texture createTextureCube(UINT width, UINT height, DXGI_FORMAT format, UINT levels=0) const;

	void createTextureUAV(Texture& texture, UINT mipSlice) const;

	FrameBuffer createFrameBuffer(UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat) const;
	void resolveFrameBuffer(const FrameBuffer& srcfb, const FrameBuffer& dstfb, DXGI_FORMAT format) const;

	ComPtr<ID3D11Buffer> createConstantBuffer(const void* data, UINT size) const;
	template<typename T> ComPtr<ID3D11Buffer> createConstantBuffer(const T* data=nullptr) const
	{
		static_assert(sizeof(T) == Utility::roundToPowerOfTwo(sizeof(T), 16));
		return createConstantBuffer(data, sizeof(T));
	}
	
	static ComPtr<ID3DBlob> compileShader(const std::string& filename, const std::string& entryPoint, const std::string& profile);

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11DeviceContext> m_context;
	ComPtr<IDXGISwapChain> m_swapChain;
	ComPtr<ID3D11RenderTargetView> m_backBufferRTV;
	
	FrameBuffer m_framebuffer;
	FrameBuffer m_resolveFramebuffer;

	ComPtr<ID3D11RasterizerState> m_defaultRasterizerState;
	ComPtr<ID3D11DepthStencilState> m_defaultDepthStencilState;
	ComPtr<ID3D11DepthStencilState> m_skyboxDepthStencilState;
	
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11SamplerState> m_computeSampler;
	ComPtr<ID3D11SamplerState> m_spBRDF_Sampler;

	ComPtr<ID3D11Buffer> m_transformCB;
	ComPtr<ID3D11Buffer> m_shadingCB;
	
	ShaderProgram m_pbrProgram;
	ShaderProgram m_skyboxProgram;
	ShaderProgram m_tonemapProgram;
	
	MeshBuffer m_pbrModel;
	MeshBuffer m_skybox;

	Texture m_albedoTexture;
	Texture m_normalTexture;
	Texture m_metalnessTexture;
	Texture m_roughnessTexture;

	Texture m_envTexture;
	Texture m_irmapTexture;
	Texture m_spBRDF_LUT;
};

} // D3D11

#endif // ENABLE_D3D11
