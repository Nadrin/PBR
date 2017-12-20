/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#pragma once

#include <memory>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "common/renderer.hpp"
#include "common/utils.hpp"

namespace D3D12 {

using Microsoft::WRL::ComPtr;

struct Descriptor
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};

struct DescriptorHeap
{
	ComPtr<ID3D12DescriptorHeap> heap;
	UINT descriptorSize;
	UINT numDescriptorsInHeap;
	UINT numDescriptorsAllocated;

	Descriptor alloc()
	{
		return (*this)[numDescriptorsAllocated++];
	}
	Descriptor operator[](UINT index) const
	{
		assert(index < numDescriptorsInHeap);
		return {
			D3D12_CPU_DESCRIPTOR_HANDLE{heap->GetCPUDescriptorHandleForHeapStart().ptr + index * descriptorSize},
			D3D12_GPU_DESCRIPTOR_HANDLE{heap->GetGPUDescriptorHandleForHeapStart().ptr + index * descriptorSize}
		};
	}
};

struct DescriptorHeapMark
{
	DescriptorHeapMark(DescriptorHeap& heap)
		: heap(heap)
		, mark(heap.numDescriptorsAllocated)
	{}
	~DescriptorHeapMark()
	{
		heap.numDescriptorsAllocated = mark;
	}
	DescriptorHeap& heap;
	const UINT mark;
};

struct StagingBuffer
{
	ComPtr<ID3D12Resource> buffer;
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
	UINT firstSubresource;
	UINT numSubresources;
};

struct UploadBuffer
{
	ComPtr<ID3D12Resource> buffer;
	UINT capacity;
	UINT cursor;
	uint8_t* cpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
};

struct UploadBufferRegion
{
	void* cpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
	UINT size;
};

struct MeshBuffer
{
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vbv;
	D3D12_INDEX_BUFFER_VIEW ibv;
	UINT numElements;
};

struct FrameBuffer
{
	ComPtr<ID3D12Resource> colorTexture;
	ComPtr<ID3D12Resource> depthStencilTexture;
	Descriptor rtv;
	Descriptor dsv;
	Descriptor srv;
	UINT width, height;
	UINT samples;
};

struct SwapChainBuffer
{
	ComPtr<ID3D12Resource> buffer;
	Descriptor rtv;
};

struct ConstantBufferView
{
	UploadBufferRegion data;
	Descriptor cbv;

	template<typename T> T* as() const
	{
		return reinterpret_cast<T*>(data.cpuAddress);
	}
};

struct Texture
{
	ComPtr<ID3D12Resource> texture;
	Descriptor srv;
	Descriptor uav;
	UINT width, height;
	UINT levels;
};

class Renderer final : public RendererInterface
{
public:
	GLFWwindow* initialize(int width, int height, int maxSamples) override;
	void shutdown() override;
	void setup() override;
	void render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene) override;

private:
	DescriptorHeap createDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc) const;

	MeshBuffer createMeshBuffer(const std::shared_ptr<class Mesh>& mesh) const;
	MeshBuffer createClipSpaceQuad() const;
	UploadBuffer createUploadBuffer(UINT capacity) const;
	UploadBufferRegion allocFromUploadBuffer(UploadBuffer& buffer, UINT size, int align) const;
	StagingBuffer createStagingBuffer(const ComPtr<ID3D12Resource>& resource, UINT firstSubresource, UINT numSubresources, const D3D12_SUBRESOURCE_DATA* data) const;

	Texture createTexture(UINT width, UINT height, UINT depth, DXGI_FORMAT format, UINT levels=0);
	Texture createTexture(const std::shared_ptr<class Image>& image, DXGI_FORMAT format, UINT levels=0);
	void generateMipmaps(const Texture& texture);

	void createTextureSRV(Texture& texture, D3D12_SRV_DIMENSION dimension, UINT mostDetailedMip=0, UINT mipLevels=0);
	void createTextureUAV(Texture& texture, UINT mipSlice);
	
	FrameBuffer createFrameBuffer(UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat);
	void resolveFrameBuffer(const FrameBuffer& srcfb, const FrameBuffer& dstfb, DXGI_FORMAT format) const;

	ComPtr<ID3D12RootSignature> createRootSignature(D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc) const;
	
	ConstantBufferView createConstantBufferView(const void* data, UINT size);
	template<typename T> ConstantBufferView createConstantBufferView(const T* data=nullptr)
	{
		return createConstantBufferView(data, sizeof(T));
	}

	void executeCommandList(bool reset=true) const;
	void waitForGPU() const;
	void presentFrame();
	
	static ComPtr<IDXGIAdapter1> getAdapter(const ComPtr<IDXGIFactory4>& factory);
	static ComPtr<ID3DBlob> compileShader(const std::string& filename, const std::string& entryPoint, const std::string& profile);
	
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	DescriptorHeap m_descHeapRTV;
	DescriptorHeap m_descHeapDSV;
	DescriptorHeap m_descHeapCBV_SRV_UAV;
	
	UploadBuffer m_constantBuffer;

	static const UINT NumFrames = 2;
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[NumFrames];
	SwapChainBuffer m_backbuffers[NumFrames];
	FrameBuffer m_framebuffers[NumFrames];
	FrameBuffer m_resolveFramebuffers[NumFrames];
	ConstantBufferView m_transformCBVs[NumFrames];
	ConstantBufferView m_shadingCBVs[NumFrames];
	
	struct {
		ComPtr<ID3D12RootSignature> rootSignature;
		ComPtr<ID3D12PipelineState> linearTexturePipelineState;
		ComPtr<ID3D12PipelineState> gammaTexturePipelineState;
		ComPtr<ID3D12PipelineState> arrayTexturePipelineState;
	} m_mipmapGeneration;

	MeshBuffer m_screenQuad;
	MeshBuffer m_pbrModel;
	MeshBuffer m_skybox;

	Texture m_albedoTexture;
	Texture m_normalTexture;
	Texture m_metalnessTexture;
	Texture m_roughnessTexture;

	Texture m_envTexture;
	Texture m_irmapTexture;
	Texture m_spBRDF_LUT;

	ComPtr<ID3D12RootSignature> m_tonemapRootSignature;
	ComPtr<ID3D12PipelineState> m_tonemapPipelineState;
	ComPtr<ID3D12RootSignature> m_pbrRootSignature;
	ComPtr<ID3D12PipelineState> m_pbrPipelineState;
	ComPtr<ID3D12RootSignature> m_skyboxRootSignature;
	ComPtr<ID3D12PipelineState> m_skyboxPipelineState;

	UINT m_frameIndex;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceCompletionEvent;
	mutable UINT64 m_fenceValues[NumFrames] = {};

	D3D_ROOT_SIGNATURE_VERSION m_rootSignatureVersion;
};

} // D3D12