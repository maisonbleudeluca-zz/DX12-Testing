#define GLFW_EXPOSE_NATIVE_WIN32
#include <iostream>
#include<d3d12.h>
#include<dxgi1_6.h>
#include<GLFW/glfw3.h>
#include<GLFW/glfw3native.h>
#include <wrl/client.h>

#pragma comment(lib ,"DXGI.lib")
#pragma comment(lib ,"D3D12.lib")

unsigned width = 640;
unsigned height = 640;
static const UINT backbufferCount = 2;

GLFWwindow* window;

using Microsoft::WRL::ComPtr;

// DX Handles
ComPtr<IDXGISwapChain4> swapchain;
D3D12_VIEWPORT viewport;
D3D12_RECT surfaceSize;
ID3D12GraphicsCommandList* commandList;
ID3D12CommandAllocator* commandAllocator = nullptr;
ID3D12CommandQueue* commandQueue = nullptr;
ID3D12Device8* device = nullptr;
IDXGIAdapter4* adapter = nullptr;
IDXGIFactory7* factory = nullptr;

// Frame Handles
UINT frameIndex;
HANDLE fenceEvent;
ID3D12Fence* fence;
UINT64 fenceValue;
UINT currentBuffer;
ID3D12DescriptorHeap* renderTargetViewHeap;
ID3D12Resource* renderTargets[backbufferCount];
UINT rtvDescriptorSize;

// DX Debug Handles
ID3D12DebugDevice1* debugDevice = nullptr;
ID3D12Debug1* debugController = nullptr;

inline void ThrowIfFailed (HRESULT hr)
{
	if (FAILED (hr))
	{
		std::cout << std::system_category ().message (hr).c_str ();
		throw std::exception ();
	}
}

void CreateAppWindow ()
{
	glfwInit ();

	glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);

	window = glfwCreateWindow (width, height, "Hello Triangle", nullptr, nullptr);
}


void CreateFactory ()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)	
	ID3D12Debug* dc;


	ThrowIfFailed (D3D12GetDebugInterface (IID_PPV_ARGS (&dc)));
	ThrowIfFailed (dc->QueryInterface (IID_PPV_ARGS (&debugController)));
	debugController->EnableDebugLayer ();
	debugController->SetEnableGPUBasedValidation (true);

	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

	dc->Release ();
	dc = nullptr;
#endif

	HRESULT result = CreateDXGIFactory2 (dxgiFactoryFlags, IID_PPV_ARGS (&factory));
}

void CreateAdapter ()
{

	for (UINT adapterIndex = 0; factory->EnumAdapterByGpuPreference (adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS (&adapter)) != DXGI_ERROR_NOT_FOUND; adapterIndex++)
	{
		DXGI_ADAPTER_DESC3 adapterDesc;

		adapter->GetDesc3 (&adapterDesc);

		std::cout << adapterDesc.VendorId << "\n";

		if (SUCCEEDED (D3D12CreateDevice (adapter, D3D_FEATURE_LEVEL_12_0, _uuidof (ID3D12Device), nullptr)))
		{
			break;
		}
		adapter->Release ();
	}
}

void CreateDevice ()
{
	ThrowIfFailed (D3D12CreateDevice (adapter, D3D_FEATURE_LEVEL_12_0,
									  IID_PPV_ARGS (&device)));

	device->SetName (L"Main Device");

#if defined(_DEBUG)
	// Get debug device
	ThrowIfFailed (device->QueryInterface (&debugDevice));
#endif
}

void CreateCommandQueue ()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed (device->CreateCommandQueue (&queueDesc, IID_PPV_ARGS (&commandQueue)));
}

void CreateCommandAllocator ()
{
	ThrowIfFailed (device->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS (&commandAllocator)));
}

void CreateFence ()
{
	ThrowIfFailed (device->CreateFence (0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&fence)));
}

void CreateBarrier ()
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = renderTargets[frameIndex];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList->ResourceBarrier (1, &barrier);
}

void CreateSwapChain ()
{
	surfaceSize.left = 0;
	surfaceSize.top = 0;
	surfaceSize.right = static_cast<LONG>(width);
	surfaceSize.bottom = static_cast<LONG>(height);

	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = .1f;
	viewport.MaxDepth = 1000.f;

	if (swapchain != nullptr)
	{
		swapchain->ResizeBuffers (backbufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	}
	else
	{
		DXGI_SWAP_CHAIN_DESC1 swapchainDesc{};
		swapchainDesc.BufferCount = backbufferCount;
		swapchainDesc.Width = width;
		swapchainDesc.Height = height;
		swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchainDesc.SampleDesc.Count = 1;
		swapchainDesc.Scaling = DXGI_SCALING_NONE;

		ComPtr<IDXGISwapChain1> swapChain1;

		ThrowIfFailed (factory->CreateSwapChainForHwnd (commandQueue, glfwGetWin32Window (window), &swapchainDesc, nullptr, nullptr, &swapChain1));

		ThrowIfFailed (factory->MakeWindowAssociation (glfwGetWin32Window (window), DXGI_MWA_NO_ALT_ENTER));


		ThrowIfFailed (swapChain1.As (&swapchain));

		frameIndex = swapchain->GetCurrentBackBufferIndex ();

		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = backbufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed (device->CreateDescriptorHeap (
			&rtvHeapDesc, IID_PPV_ARGS (&renderTargetViewHeap)));

		rtvDescriptorSize =
			device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// 🎞️ Create frame resources

		D3D12_CPU_DESCRIPTOR_HANDLE
			rtvHandle (renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart ());

		// Create a RTV for each frame.
		for (UINT n = 0; n < backbufferCount; n++)
		{
			ThrowIfFailed (swapchain->GetBuffer (n, IID_PPV_ARGS (&renderTargets[n])));
			device->CreateRenderTargetView (renderTargets[n], nullptr, rtvHandle);
			rtvHandle.ptr += (1 * rtvDescriptorSize);
		}
	}
}

int main ()
{
	CreateAppWindow ();
	CreateFactory ();
	CreateAdapter ();
	CreateDevice ();
	CreateCommandQueue ();
	CreateCommandAllocator ();
	CreateFence ();
	CreateBarrier ();
	CreateSwapChain ();
}