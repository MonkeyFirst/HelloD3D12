#include <Windows.h>
#include <tchar.h>
#include <wrl/client.h>
#include <stdexcept>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace std;
using Microsoft::WRL::ComPtr;

namespace
{
	const int WINDOW_WIDTH = 400;
	const int WINDOW_HEIGHT = 240;
	HWND g_mainWindowHandle = 0;
};

void CHK(HRESULT hr)
{
	if (FAILED(hr))
		throw runtime_error("HRESULT is failed value.");
}

class D3D
{
	ComPtr<IDXGIFactory2> mDxgiFactory;
	ComPtr<IDXGISwapChain1> mSwapChain;
	ComPtr<ID3D12Resource> mD3DBuffer;
	int mBufferWidth, mBufferHeight;

	ID3D12Device* mDev;
	ComPtr<ID3D12CommandAllocator> mCmdAlloc;
	ComPtr<ID3D12CommandQueue> mCmdQueue;

	ComPtr<ID3D12GraphicsCommandList> mCmdList;

	ComPtr<ID3D12DescriptorHeap> mDescHeapRtv;
	ComPtr<ID3D12DescriptorHeap> mDescHeapCbvSrvUav;

public:
	D3D(int width, int height, HWND hWnd)
		: mBufferWidth(width), mBufferHeight(height), mDev(nullptr)
	{
		{
			IDXGIFactory1* dxgiFactory1;
			IDXGIFactory2* dxgiFactory2;
			CHK(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory1)));
			CHK(dxgiFactory1->QueryInterface(IID_PPV_ARGS(&dxgiFactory2)));
			dxgiFactory1->Release();
			mDxgiFactory = dxgiFactory2;
		}

		D3D12_CREATE_DEVICE_FLAG createFlag = D3D12_CREATE_DEVICE_NONE;
#if _DEBUG
		createFlag = D3D12_CREATE_DEVICE_DEBUG;
#endif /* _DEBUG */
		ID3D12Device* dev;
		CHK(D3D12CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			createFlag,
			D3D_FEATURE_LEVEL_11_1,
			D3D12_SDK_VERSION,
			IID_PPV_ARGS(&dev)));
		mDev = dev;

		DXGI_SWAP_CHAIN_DESC1 scDesc = {};
		scDesc.Width = width;
		scDesc.Height = height;
		scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.SampleDesc.Count = 1;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.BufferCount = 1;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		IDXGISwapChain1* swapChain;
		CHK(mDxgiFactory->CreateSwapChainForHwnd(dev, hWnd, &scDesc, nullptr, nullptr, &swapChain));
		mSwapChain = swapChain;

		ID3D12CommandAllocator* cmdAlloc;
		CHK(mDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)));
		mCmdAlloc = cmdAlloc;

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ID3D12CommandQueue* cmdQueue;
		CHK(mDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));
		mCmdQueue = cmdQueue;

		ID3D12GraphicsCommandList* graphicsCmdList;
		CHK(mDev->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mCmdAlloc.Get(),
			nullptr,
			IID_PPV_ARGS(&graphicsCmdList)));
		mCmdList = graphicsCmdList;

		ID3D12Resource* d3dBuffer;
		CHK(mSwapChain->GetBuffer(0, IID_PPV_ARGS(&d3dBuffer)));
		d3dBuffer->SetName(L"SwapChain Buffer");
		mD3DBuffer = d3dBuffer;

		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = D3D12_RTV_DESCRIPTOR_HEAP;
			desc.NumDescriptors = 10;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_SHADER_VISIBLE;
			desc.NodeMask = 0;
			ID3D12DescriptorHeap* heap;
			CHK(mDev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
			mDescHeapRtv = heap;

			desc.Type = D3D12_CBV_SRV_UAV_DESCRIPTOR_HEAP;
			desc.NumDescriptors = 100;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_SHADER_VISIBLE;
			desc.NodeMask = 0;
			CHK(mDev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
			mDescHeapCbvSrvUav = heap;
		}

		mDev->CreateRenderTargetView(d3dBuffer, nullptr, mDescHeapRtv->GetCPUDescriptorHandleForHeapStart());
	}
	~D3D()
	{
	}
	ID3D12Device* GetDevice() const
	{
		return mDev;
	}
	void Draw()
	{
		D3D12_CPU_DESCRIPTOR_HANDLE descHandleRtv = mDescHeapRtv->GetCPUDescriptorHandleForHeapStart();

		ID3D12DescriptorHeap* descHeapRtv[] = { mDescHeapCbvSrvUav.Get()/*, mDescHeapRtv.Get()*/ };
		mCmdList->SetDescriptorHeaps(descHeapRtv, ARRAYSIZE(descHeapRtv));

		// Barrier Present -> RenderTarget
		setResourceBarrier(mCmdList.Get(), mD3DBuffer.Get(), D3D12_RESOURCE_USAGE_PRESENT, D3D12_RESOURCE_USAGE_RENDER_TARGET);

		// Viewport
		D3D12_VIEWPORT viewport = {};
		viewport.Width = (float)mBufferWidth;
		viewport.Height = (float)mBufferHeight;
		mCmdList->RSSetViewports(1, &viewport);

		// Clear
		{
			auto saturate = [](float a) { return a < 0 ? 0 : a > 1 ? 1 : a; };
			float clearColor[4];
			static float h = 0.0f;
			h += 0.02f;
			if (h >= 1) h = 0.0f;
			clearColor[0] = saturate(std::abs(h * 6.0f - 3.0f) - 1.0f);
			clearColor[1] = saturate(2.0f - std::abs(h * 6.0f - 2.0f));
			clearColor[2] = saturate(2.0f - std::abs(h * 6.0f - 4.0f));
			mCmdList->ClearRenderTargetView(descHandleRtv, clearColor, nullptr, 0);
		}

		// Barrier RenderTarget -> Present
		setResourceBarrier(mCmdList.Get(), mD3DBuffer.Get(), D3D12_RESOURCE_USAGE_RENDER_TARGET, D3D12_RESOURCE_USAGE_PRESENT);

		// Exec
		CHK(mCmdList->Close());
		ID3D12CommandList* const cmdList = mCmdList.Get();
		mCmdQueue->ExecuteCommandLists(1, &cmdList);

		CHK(mSwapChain->Present(1, 0));
		Sleep(100);

		CHK(mCmdAlloc->Reset());
		CHK(mCmdList->Reset(mCmdAlloc.Get(), nullptr));
	}
	
private:
	void setResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* res, UINT before, UINT after)
	{
		D3D12_RESOURCE_BARRIER_DESC desc = {};
		desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		desc.Transition.pResource = res;
		desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		desc.Transition.StateBefore = before;
		desc.Transition.StateAfter = after;
		commandList->ResourceBarrier(1, &desc);
	}
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message) {
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			PostMessage(hWnd, WM_DESTROY, 0, 0);
			return 0;
		}
		break;

	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

static HWND setupWindow(int width, int height)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = (HMODULE)GetModuleHandle(0);
	wcex.hIcon = nullptr;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = _T("WindowClass");
	wcex.hIconSm = nullptr;
	if (!RegisterClassEx(&wcex)) {
		throw runtime_error("RegisterClassEx()");
	}

	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	const int windowWidth = (rect.right - rect.left);
	const int windowHeight = (rect.bottom - rect.top);

	HWND hWnd = CreateWindow(_T("WindowClass"), _T("Window"),
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, windowWidth, windowHeight,
		nullptr, nullptr, nullptr, nullptr);
	if (!hWnd) {
		throw runtime_error("CreateWindow()");
	}

	return hWnd;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	MSG msg;
	ZeroMemory(&msg, sizeof msg);

	ID3D12Device* dev;

#ifdef NDEBUG
	try
#endif
	{
		g_mainWindowHandle = setupWindow(WINDOW_WIDTH, WINDOW_HEIGHT);
		ShowWindow(g_mainWindowHandle, SW_SHOW);
		UpdateWindow(g_mainWindowHandle);

		D3D d3d(WINDOW_WIDTH, WINDOW_HEIGHT, g_mainWindowHandle);
		dev = d3d.GetDevice();

		while (msg.message != WM_QUIT) {
			BOOL r = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
			if (r == 0) {
				d3d.Draw();
			}
			else {
				DispatchMessage(&msg);
			}
		}
	}
#ifdef NDEBUG
	catch (std::exception &e) {
		MessageBoxA(g_mainWindowHandle, e.what(), "Exception occuured.", MB_ICONSTOP);
	}
#endif

	if (dev)
		dev->Release();

	return static_cast<int>(msg.wParam);
}

