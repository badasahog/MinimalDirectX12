/*
* (C) 2024-2025 badasahog. All Rights Reserved
*
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#undef _CRT_SECURE_NO_WARNINGS
#include <shellscalingapi.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <cglm/call.h>
#include <cglm/cam.h>
#include <cglm/clipspace/persp_lh_zo.h>
#include <cglm/clipspace/view_lh.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdalign.h>

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:Shcore.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:dxguid.lib")

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) UINT D3D12SDKVersion = 612;
__declspec(dllexport) char* D3D12SDKPath = ".\\D3D12\\";

HANDLE ConsoleHandle;
ID3D12Device10* Device;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (hr == 0x887A0005)//device removed
	{
		THROW_ON_FAIL_IMPL(ID3D12Device10_GetDeviceRemovedReason(Device), line);
	}

	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			NULL
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, NULL, NULL);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, NULL, NULL);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, NULL, NULL);
			WriteConsoleA(ConsoleHandle, "\n", 1, NULL, NULL);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == NULL || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

inline void MEMCPY_VERIFY_IMPL(errno_t error, int line)
{
	if (error != 0)
	{
		char buffer[28];
		int stringlength = _snprintf_s(buffer, 28, _TRUNCATE, "memcpy failed on line %i\n", line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);
		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define MEMCPY_VERIFY(x) MEMCPY_VERIFY_IMPL(x, __LINE__)

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const bool bWarp = false;
static const LPCTSTR WindowClassName = L"MinimalDx12";

#define BUFFER_COUNT 3
#define WM_INIT (WM_USER + 1)

struct Vertex {
	vec3 pos;
	vec2 texCoord;
};

static const struct Vertex VertexList[] = {
	{ -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
	{  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
	{ -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
	{  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

	{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
	{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
	{  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
	{  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

	{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
	{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
	{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
	{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

	{  0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
	{ -0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
	{  0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
	{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f },

	{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f },
	{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
	{  0.5f,  0.5f, -0.5f, 1.0f, 1.0f },
	{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },

	{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f },
	{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
	{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
	{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f },
};

static const WORD IndexList[] = {
	0, 1, 2,
	0, 3, 1,
	4, 5, 6,
	4, 7, 5,
	8, 9, 10,
	8, 11, 9,
	12, 13, 14,
	12, 15, 13,
	16, 17, 18,
	16, 19, 17,
	20, 21, 22,
	20, 23, 21,
};

static_assert(ARRAYSIZE(IndexList) < 256, "");

static const int NUM_CUBE_INDICES = ARRAYSIZE(IndexList);

static const UINT TEXTURE_WIDTH = 64;
static const UINT TEXTURE_HEIGHT = 64;
static const UINT BYTES_PER_TEXEL = 2;
static const DXGI_FORMAT TEXTURE_FORMAT = DXGI_FORMAT_B5G6R5_UNORM;

static const DXGI_FORMAT RTV_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DSV_FORMAT = DXGI_FORMAT_D16_UNORM;

static const UINT64 ConstantBufferPerObjectAlignedSize = (sizeof(mat4) + 255) & ~255;

struct DxObjects
{
	IDXGISwapChain3* SwapChain;
	ID3D12CommandQueue* CommandQueue;

	UINT RtvDescriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHeapHandle;
	ID3D12Resource* RenderTargets[BUFFER_COUNT];

	ID3D12CommandAllocator* CommandAllocator;
	ID3D12GraphicsCommandList7* CommandList;
	ID3D12PipelineState* PipelineStateObject;

	ID3D12RootSignature* RootSignature;

	ID3D12Resource* VertexBuffer;
	ID3D12Resource* IndexBuffer;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView;

	ID3D12Resource* DepthStencilBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE DsvHeapHandle;

	UINT8* ConstantBufferCPUAddress[BUFFER_COUNT];
	D3D12_GPU_VIRTUAL_ADDRESS ContantBufferGPUAddress[BUFFER_COUNT];

	ID3D12DescriptorHeap* SRVDescriptorHeap;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;
};

struct SyncObjects
{
	ID3D12Fence* Fence[BUFFER_COUNT];
	HANDLE FenceEvent;
	UINT64 FenceValue[BUFFER_COUNT];
	int FrameIndex;
};

struct WindowProcPayload
{
	struct DxObjects* DxObjects;
	struct SyncObjects* SyncObjects;
};

inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects, struct SyncObjects* restrict SyncObjects);

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	THROW_ON_FAIL(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE));
	
	HINSTANCE Instance = GetModuleHandleW(NULL);

	HICON Icon = LoadIconW(NULL, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(NULL, IDC_ARROW);

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = Instance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = NULL;
	WindowClass.lpszClassName = WindowClassName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	RECT WindowRect = { 0 };
	WindowRect.left = 0;
	WindowRect.top = 0;
	WindowRect.right = 800;
	WindowRect.bottom = 600;

	THROW_ON_FALSE(AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE));

	HWND Window = CreateWindowExW(
		0,
		WindowClassName,
		L"Minimal DirectX 12",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowRect.right - WindowRect.left,
		WindowRect.bottom - WindowRect.top,
		NULL,
		NULL,
		Instance,
		NULL);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

#ifdef _DEBUG
	ID3D12Debug6* DebugController;

	{
		ID3D12Debug* DebugControllerV1;
		THROW_ON_FAIL(D3D12GetDebugInterface(&IID_ID3D12Debug, &DebugControllerV1));
		THROW_ON_FAIL(ID3D12Debug_QueryInterface(DebugControllerV1, &IID_ID3D12Debug6, &DebugController));
		ID3D12Debug_Release(DebugControllerV1);
	}

	ID3D12Debug6_EnableDebugLayer(DebugController);
	ID3D12Debug6_SetEnableSynchronizedCommandQueueValidation(DebugController, TRUE);
	ID3D12Debug6_SetGPUBasedValidationFlags(DebugController, D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);
	ID3D12Debug6_SetEnableGPUBasedValidation(DebugController, TRUE);
#endif

	IDXGIFactory6* Factory;

#ifdef _DEBUG
	THROW_ON_FAIL(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, &IID_IDXGIFactory6, &Factory));
#else
	THROW_ON_FAIL(CreateDXGIFactory2(0, &IID_IDXGIFactory6, &Factory));
#endif

	struct DxObjects DxObjects = { 0 };

	IDXGIAdapter1* Adapter;

	if (bWarp)
	{
		IDXGIFactory6_EnumWarpAdapter(Factory, &IID_IDXGIAdapter1, &Adapter);
	}
	else
	{
		IDXGIFactory6_EnumAdapterByGpuPreference(Factory, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, &Adapter);
	}

	THROW_ON_FAIL(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_12_1, &IID_ID3D12Device10, &Device));

	THROW_ON_FAIL(IDXGIAdapter1_Release(Adapter));

#ifdef _DEBUG
	ID3D12InfoQueue* InfoQueue;
	THROW_ON_FAIL(ID3D12Device10_QueryInterface(Device, &IID_ID3D12InfoQueue, &InfoQueue));

	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
#endif

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &DxObjects.CommandQueue));
	}

	{
		DXGI_SWAP_CHAIN_DESC SwapChainDesc = { 0 };
		SwapChainDesc.BufferDesc.Width = 1;
		SwapChainDesc.BufferDesc.Height = 1;
		SwapChainDesc.BufferDesc.Format = RTV_FORMAT;
		SwapChainDesc.SampleDesc.Count = 1;
		SwapChainDesc.SampleDesc.Quality = 0;
		SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapChainDesc.BufferCount = BUFFER_COUNT;
		SwapChainDesc.OutputWindow = Window;
		SwapChainDesc.Windowed = TRUE;
		SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		THROW_ON_FAIL(IDXGIFactory6_CreateSwapChain(Factory, DxObjects.CommandQueue, &SwapChainDesc, &DxObjects.SwapChain));
	}

	THROW_ON_FAIL(IDXGIFactory6_MakeWindowAssociation(Factory, Window, DXGI_MWA_NO_ALT_ENTER));
	THROW_ON_FAIL(IDXGIFactory6_Release(Factory));
	
	struct SyncObjects SyncObjects = { 0 };

	SyncObjects.FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects.SwapChain);

	ID3D12DescriptorHeap* RtvDescriptorHeap;

	{
		D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc = { 0 };
		RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		RtvHeapDesc.NumDescriptors = BUFFER_COUNT;
		RtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &RtvHeapDesc, &IID_ID3D12DescriptorHeap, &RtvDescriptorHeap));
	}

	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(RtvDescriptorHeap, &DxObjects.RtvHeapHandle);
	
	DxObjects.RtvDescriptorSize = ID3D12Device10_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &DxObjects.CommandAllocator));
	
	THROW_ON_FAIL(ID3D12Device10_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, DxObjects.CommandAllocator, NULL, &IID_ID3D12GraphicsCommandList7, &DxObjects.CommandList));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device10_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &SyncObjects.Fence[i]));
		SyncObjects.FenceValue[i] = 0;
	}

	SyncObjects.FenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	VALIDATE_HANDLE(SyncObjects.FenceEvent);

	{
		D3D12_DESCRIPTOR_RANGE1  DescriptorRange = { 0 };
		DescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		DescriptorRange.NumDescriptors = 1;
		DescriptorRange.BaseShaderRegister = 0;
		DescriptorRange.RegisterSpace = 0;
		DescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		DescriptorRange.OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER1  RootParameters[2] = { 0 };
		RootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		RootParameters[0].Descriptor.ShaderRegister = 0;
		RootParameters[0].Descriptor.RegisterSpace = 0;
		RootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		RootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[1].DescriptorTable.pDescriptorRanges = &DescriptorRange;
		RootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC Sampler = { 0 };
		Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Sampler.MipLODBias = 0;
		Sampler.MaxAnisotropy = 0;
		Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		Sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		Sampler.MinLOD = 0.0f;
		Sampler.MaxLOD = D3D12_FLOAT32_MAX;
		Sampler.ShaderRegister = 0;
		Sampler.RegisterSpace = 0;
		Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootSignatureDesc = { 0 };
		RootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		RootSignatureDesc.Desc_1_1.NumParameters = ARRAYSIZE(RootParameters);
		RootSignatureDesc.Desc_1_1.pParameters = RootParameters;
		RootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
		RootSignatureDesc.Desc_1_1.pStaticSamplers = &Sampler;
		RootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ID3D10Blob* Signature;
		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&RootSignatureDesc, &Signature, NULL));
		THROW_ON_FAIL(ID3D12Device10_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(Signature), ID3D10Blob_GetBufferSize(Signature), &IID_ID3D12RootSignature, &DxObjects.RootSignature));
		THROW_ON_FAIL(ID3D10Blob_Release(Signature));
	}

	HANDLE VertexShaderFile = CreateFileW(L"VertexShader.cso", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	VALIDATE_HANDLE(VertexShaderFile);

	LONGLONG VertexShaderSize;
	THROW_ON_FALSE(GetFileSizeEx(VertexShaderFile, &VertexShaderSize));

	HANDLE VertexShaderFileMap = CreateFileMappingW(VertexShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
	VALIDATE_HANDLE(VertexShaderFileMap);

	const void* VertexShaderBytecode = MapViewOfFile(VertexShaderFileMap, FILE_MAP_READ, 0, 0, 0);


	HANDLE PixelShaderFile = CreateFileW(L"PixelShader.cso", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	VALIDATE_HANDLE(PixelShaderFile);

	LONGLONG PixelShaderSize;
	THROW_ON_FALSE(GetFileSizeEx(PixelShaderFile, &PixelShaderSize));

	HANDLE PixelShaderFileMap = CreateFileMappingW(PixelShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
	VALIDATE_HANDLE(PixelShaderFileMap);

	const void* PixelShaderBytecode = MapViewOfFile(PixelShaderFileMap, FILE_MAP_READ, 0, 0, 0);

	struct
	{
		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypepRootSignature;
		ID3D12RootSignature* pRootSignature;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeInputLayout;
		D3D12_INPUT_LAYOUT_DESC InputLayout;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeVS;
		D3D12_SHADER_BYTECODE VS;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypePS;
		D3D12_SHADER_BYTECODE PS;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeDepthStencilState;
		D3D12_DEPTH_STENCIL_DESC DepthStencilState;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeDSVFormat;
		DXGI_FORMAT DSVFormat;

		alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeRTVFormats;
		struct D3D12_RT_FORMAT_ARRAY RTVFormats;
	} PipelineStateObject = { 0 };

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc = { 0 };
	PipelineStateObject.ObjectTypepRootSignature = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
	PipelineStateObject.pRootSignature = DxObjects.RootSignature;

	PipelineStateObject.ObjectTypeVS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
	PipelineStateObject.VS.pShaderBytecode = VertexShaderBytecode;
	PipelineStateObject.VS.BytecodeLength = VertexShaderSize;

	PipelineStateObject.ObjectTypePS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
	PipelineStateObject.PS.pShaderBytecode = PixelShaderBytecode;
	PipelineStateObject.PS.BytecodeLength = PixelShaderSize;

	PipelineStateObject.ObjectTypeDepthStencilState = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
	PipelineStateObject.DepthStencilState.DepthEnable = TRUE;
	PipelineStateObject.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	PipelineStateObject.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	PipelineStateObject.DepthStencilState.StencilEnable = FALSE;
	PipelineStateObject.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	PipelineStateObject.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	PipelineStateObject.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	PipelineStateObject.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	PipelineStateObject.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	PipelineStateObject.ObjectTypeInputLayout = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT;
	PipelineStateObject.InputLayout.pInputElementDescs = (D3D12_INPUT_ELEMENT_DESC[]){
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }};
	PipelineStateObject.InputLayout.NumElements = 2;

	PipelineStateObject.ObjectTypeDSVFormat = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
	PipelineStateObject.DSVFormat = DSV_FORMAT;

	PipelineStateObject.ObjectTypeRTVFormats = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
	PipelineStateObject.RTVFormats.RTFormats[0] = RTV_FORMAT;
	PipelineStateObject.RTVFormats.NumRenderTargets = 1;

	D3D12_PIPELINE_STATE_STREAM_DESC PsoStreamDesc = { 0 };
	PsoStreamDesc.SizeInBytes = sizeof(PipelineStateObject);
	PsoStreamDesc.pPipelineStateSubobjectStream = &PipelineStateObject;

	THROW_ON_FAIL(ID3D12Device10_CreatePipelineState(Device, &PsoStreamDesc, &IID_ID3D12PipelineState, &DxObjects.PipelineStateObject));

	THROW_ON_FALSE(UnmapViewOfFile(VertexShaderBytecode));
	THROW_ON_FALSE(CloseHandle(VertexShaderFileMap));
	THROW_ON_FALSE(CloseHandle(VertexShaderFile));
	
	THROW_ON_FALSE(UnmapViewOfFile(PixelShaderBytecode));
	THROW_ON_FALSE(CloseHandle(PixelShaderFileMap));
	THROW_ON_FALSE(CloseHandle(PixelShaderFile));

	ID3D12Resource* VertexBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = sizeof(VertexList);
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &DxObjects.VertexBuffer));
		}

#ifdef _DEBUG
		ID3D12Resource_SetName(DxObjects.VertexBuffer, L"Vertex Buffer Resource");
#endif

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &VertexBufferUploadHeap));
		}

#ifdef _DEBUG
		ID3D12Resource_SetName(VertexBufferUploadHeap, L"Vertex Buffer Upload Heap");
#endif
	}
	
	{
		void* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(VertexBufferUploadHeap, 0, NULL, &pData));
		MEMCPY_VERIFY(memcpy_s(pData, sizeof(VertexList), VertexList, sizeof(VertexList)));
		ID3D12Resource_Unmap(VertexBufferUploadHeap, 0, NULL);

		ID3D12GraphicsCommandList7_CopyBufferRegion(DxObjects.CommandList, DxObjects.VertexBuffer, 0, VertexBufferUploadHeap, 0, sizeof(VertexList));
	}

	{
		D3D12_BUFFER_BARRIER BufferBarrier = { 0 };
		BufferBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		BufferBarrier.SyncAfter = D3D12_BARRIER_SYNC_DRAW;
		BufferBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		BufferBarrier.AccessAfter = D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		BufferBarrier.pResource = DxObjects.VertexBuffer;
		BufferBarrier.Offset = 0;
		BufferBarrier.Size = sizeof(VertexList);

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_BUFFER;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pBufferBarriers = &BufferBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	ID3D12Resource* IndexBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = sizeof(IndexList);
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &DxObjects.IndexBuffer));
		}

#ifdef _DEBUG
		ID3D12Resource_SetName(DxObjects.IndexBuffer, L"Index Buffer Resource");
#endif

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &IndexBufferUploadHeap));
		}

#ifdef _DEBUG
		ID3D12Resource_SetName(IndexBufferUploadHeap, L"Index Buffer Upload Heap");
#endif
	}

	{
		void* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(IndexBufferUploadHeap, 0, NULL, &pData));
		MEMCPY_VERIFY(memcpy_s(pData, sizeof(IndexList), IndexList, sizeof(IndexList)));
		ID3D12Resource_Unmap(IndexBufferUploadHeap, 0, NULL);

		ID3D12GraphicsCommandList7_CopyBufferRegion(DxObjects.CommandList, DxObjects.IndexBuffer, 0, IndexBufferUploadHeap, 0, sizeof(IndexList));
	}

	{
		D3D12_BUFFER_BARRIER BufferBarrier = { 0 };
		BufferBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		BufferBarrier.SyncAfter = D3D12_BARRIER_SYNC_DRAW;
		BufferBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		BufferBarrier.AccessAfter = D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		BufferBarrier.pResource = DxObjects.IndexBuffer;
		BufferBarrier.Offset = 0;
		BufferBarrier.Size = sizeof(IndexList);

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_BUFFER;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pBufferBarriers = &BufferBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	ID3D12DescriptorHeap* DepthStencilDescriptorHeap;

	{
		D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = { 0 };
		DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		DsvHeapDesc.NumDescriptors = 1;
		DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &DsvHeapDesc, &IID_ID3D12DescriptorHeap, &DepthStencilDescriptorHeap));
	}

#ifdef _DEBUG
	ID3D12DescriptorHeap_SetName(DepthStencilDescriptorHeap, L"Depth/Stencil Resource Heap");
#endif

	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DepthStencilDescriptorHeap, &DxObjects.DsvHeapHandle);

	ID3D12Resource* ConstantBufferHeaps[BUFFER_COUNT];

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = ConstantBufferPerObjectAlignedSize * 2;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &ConstantBufferHeaps[i]));

#ifdef _DEBUG
		ID3D12Resource_SetName(ConstantBufferHeaps[i], L"Constant Buffer Upload Resource Heap");
#endif

		THROW_ON_FAIL(ID3D12Resource_Map(ConstantBufferHeaps[i], 0, NULL, &DxObjects.ConstantBufferCPUAddress[i]));

		DxObjects.ContantBufferGPUAddress[i] = ID3D12Resource_GetGPUVirtualAddress(ConstantBufferHeaps[i]);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = { 0 };
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HeapDesc.NumDescriptors = 1;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, &DxObjects.SRVDescriptorHeap));
	}

	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DxObjects.SRVDescriptorHeap, &DxObjects.SrvGpuHandle);
	
	ID3D12Resource* TextureBuffer;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 TextureResourceDesc = { 0 };
		TextureResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		TextureResourceDesc.Alignment = 0;
		TextureResourceDesc.Width = TEXTURE_WIDTH;
		TextureResourceDesc.Height = TEXTURE_HEIGHT;
		TextureResourceDesc.DepthOrArraySize = 1;
		TextureResourceDesc.MipLevels = 0;
		TextureResourceDesc.Format = TEXTURE_FORMAT;
		TextureResourceDesc.SampleDesc.Count = 1;
		TextureResourceDesc.SampleDesc.Quality = 0;
		TextureResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		TextureResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &TextureResourceDesc, D3D12_BARRIER_LAYOUT_COMMON, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &TextureBuffer));
	}

#ifdef _DEBUG
	ID3D12Resource_SetName(TextureBuffer, L"Texture Buffer Resource Heap");
#endif

	ID3D12Resource* TextureBufferUploadHeap;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = TEXTURE_WIDTH * TEXTURE_HEIGHT * BYTES_PER_TEXEL;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, &IID_ID3D12Resource, &TextureBufferUploadHeap));
	}
	
#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Resource_SetName(TextureBufferUploadHeap, L"Texture Buffer Upload Resource Heap"));
#endif

	{
		WORD* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(TextureBufferUploadHeap, 0, NULL, &pData));

		for (UINT y = 0; y < TEXTURE_HEIGHT; y++)
		{
			for (UINT x = 0; x < TEXTURE_WIDTH; x++)
			{
				pData[(y * TEXTURE_WIDTH + x) * (BYTES_PER_TEXEL / sizeof(WORD))] = x == 0 || x == (TEXTURE_WIDTH - 1) || y == 0 || y == (TEXTURE_HEIGHT - 1) ? 0b1111100000000000 : rand() * (UINT16_MAX / RAND_MAX);
			}
		}
		
		ID3D12Resource_Unmap(TextureBufferUploadHeap, 0, NULL);

		D3D12_TEXTURE_COPY_LOCATION TextureCopyDest = { 0 };
		TextureCopyDest.pResource = TextureBuffer;
		TextureCopyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		TextureCopyDest.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION TextureCopySrc = { 0 };
		TextureCopySrc.pResource = TextureBufferUploadHeap;
		TextureCopySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		TextureCopySrc.PlacedFootprint.Offset = 0;
		TextureCopySrc.PlacedFootprint.Footprint.Format = TEXTURE_FORMAT;
		TextureCopySrc.PlacedFootprint.Footprint.Width = TEXTURE_WIDTH;
		TextureCopySrc.PlacedFootprint.Footprint.Height = TEXTURE_HEIGHT;
		TextureCopySrc.PlacedFootprint.Footprint.Depth = 1;
		TextureCopySrc.PlacedFootprint.Footprint.RowPitch = TEXTURE_WIDTH * BYTES_PER_TEXEL;
		ID3D12GraphicsCommandList7_CopyTextureRegion(DxObjects.CommandList, &TextureCopyDest, 0, 0, 0, &TextureCopySrc, NULL);
	}

	{
		D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
		TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
		TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
		TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
		TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
		TextureBarrier.pResource = TextureBuffer;
		TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pTextureBarriers = &TextureBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC ResourceViewDesc = { 0 };
		ResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		ResourceViewDesc.Format = TEXTURE_FORMAT;
		ResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		ResourceViewDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DxObjects.SRVDescriptorHeap, &srvHandle);
		ID3D12Device10_CreateShaderResourceView(Device, TextureBuffer, &ResourceViewDesc, srvHandle);
	}

	ID3D12GraphicsCommandList7_Close(DxObjects.CommandList);

	ID3D12CommandQueue_ExecuteCommandLists(DxObjects.CommandQueue, 1, &DxObjects.CommandList);

	SyncObjects.FenceValue[SyncObjects.FrameIndex]++;
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects.CommandQueue, SyncObjects.Fence[SyncObjects.FrameIndex], SyncObjects.FenceValue[SyncObjects.FrameIndex]));

	DxObjects.VertexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(DxObjects.VertexBuffer);
	DxObjects.VertexBufferView.SizeInBytes = sizeof(VertexList);
	DxObjects.VertexBufferView.StrideInBytes = sizeof(struct Vertex);

	DxObjects.IndexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(DxObjects.IndexBuffer);
	DxObjects.IndexBufferView.SizeInBytes = sizeof(IndexList);
	DxObjects.IndexBufferView.Format = DXGI_FORMAT_R16_UINT;

	WaitForPreviousFrame(&DxObjects, &SyncObjects);
	THROW_ON_FAIL(ID3D12Resource_Release(VertexBufferUploadHeap));
	THROW_ON_FAIL(ID3D12Resource_Release(IndexBufferUploadHeap));
	THROW_ON_FAIL(ID3D12Resource_Release(TextureBufferUploadHeap));
	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_INIT,
		.wParam = (WPARAM)&(struct WindowProcPayload)
		{
			.DxObjects = &DxObjects,
			.SyncObjects = &SyncObjects
		},
		.lParam = 0
	});

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_SIZE,
		.wParam = SIZE_RESTORED,
		.lParam = MAKELONG(WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top)
	});

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	WaitForPreviousFrame(&DxObjects, &SyncObjects);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		ID3D12Resource_Unmap(ConstantBufferHeaps[i], 0, NULL);
		THROW_ON_FAIL(ID3D12Resource_Release(ConstantBufferHeaps[i]));
	}

	THROW_ON_FAIL(ID3D12PipelineState_Release(DxObjects.PipelineStateObject));
	
	THROW_ON_FAIL(ID3D12RootSignature_Release(DxObjects.RootSignature));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.RenderTargets[i]));
	}

	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.DepthStencilBuffer));

	THROW_ON_FAIL(IDXGISwapChain3_Release(DxObjects.SwapChain));

	THROW_ON_FALSE(CloseHandle(SyncObjects.FenceEvent));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(SyncObjects.Fence[i]));
	}

	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Release(DxObjects.CommandList));

	THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.CommandAllocator));
	
	THROW_ON_FAIL(ID3D12CommandQueue_Release(DxObjects.CommandQueue));

	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(RtvDescriptorHeap));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DepthStencilDescriptorHeap)); 
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DxObjects.SRVDescriptorHeap));

	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.VertexBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.IndexBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(TextureBuffer));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12InfoQueue_Release(InfoQueue));
#endif

	THROW_ON_FAIL(ID3D12Device10_Release(Device));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Debug6_Release(DebugController));
#endif

	THROW_ON_FALSE(UnregisterClassW(WindowClassName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

#ifdef _DEBUG
	{
		IDXGIDebug1* dxgiDebug;
		THROW_ON_FAIL(DXGIGetDebugInterface1(0, &IID_IDXGIDebug1, &dxgiDebug));
		THROW_ON_FAIL(IDXGIDebug1_ReportLiveObjects(dxgiDebug, DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
#endif
	return 0;
}

LRESULT CALLBACK PreInitProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (wParam == SIZE_RESTORED)
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	static struct
	{
		UINT WindowWidth;
		UINT WindowHeight;

		bool bFullScreen;
		bool bVsync;

		D3D12_VIEWPORT Viewport;
		D3D12_RECT ScissorRect;
	} WindowDetails = { 0 };
	
	static struct
	{
		mat4 cube1RotMat;
		vec3 cube1Position;
		mat4 cube1WorldMat;

		mat4 cube2RotMat;
		vec3 cube2PositionOffset;
		mat4 cube2WorldMat;

		mat4 cameraViewMat;
		mat4 cameraProjMat;
	}
	Camera = 
	{
		.cube1Position = { 0.0f, 0.0f, 0.0f },
		.cube2PositionOffset = { 1.5f, 0.0f, 0.0f }
	};
	
	static struct
	{
		LARGE_INTEGER ProcessorFrequency;
		LARGE_INTEGER tickCount;
	} Timer = { 0 };

	static struct DxObjects* DxObjects = NULL;
	static struct SyncObjects* SyncObjects = NULL;

	switch (message)
	{
	case WM_INIT:
		WindowDetails.Viewport.TopLeftX = 0;
		WindowDetails.Viewport.TopLeftY = 0;
		WindowDetails.Viewport.MinDepth = 0.0f;
		WindowDetails.Viewport.MaxDepth = 1.0f;

		WindowDetails.ScissorRect.left = 0;
		WindowDetails.ScissorRect.top = 0;

		QueryPerformanceFrequency(&Timer.ProcessorFrequency);

		DxObjects = ((struct WindowProcPayload*)wParam)->DxObjects;
		SyncObjects = ((struct WindowProcPayload*)wParam)->SyncObjects;
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_ESCAPE:
			THROW_ON_FALSE(DestroyWindow(Window));
			break;
		case 'V':
			if (!(lParam & 1 << 30))
				WindowDetails.bVsync = !WindowDetails.bVsync;
			break;
		}
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
		{
			WindowDetails.bFullScreen = !WindowDetails.bFullScreen;

			if (WindowDetails.bFullScreen)
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, WS_EX_TOPMOST) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
			else
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, WS_OVERLAPPEDWINDOW) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
		}
		break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		if (WindowDetails.WindowWidth == LOWORD(lParam) && WindowDetails.WindowHeight == HIWORD(lParam))
			break;

		WindowDetails.WindowWidth = LOWORD(lParam);
		WindowDetails.WindowHeight = HIWORD(lParam);
		
		WindowDetails.Viewport.Width = WindowDetails.WindowWidth;
		WindowDetails.Viewport.Height = WindowDetails.WindowHeight;

		WindowDetails.ScissorRect.right = WindowDetails.WindowWidth;
		WindowDetails.ScissorRect.bottom = WindowDetails.WindowHeight;
		
		WaitForPreviousFrame(DxObjects, SyncObjects);
	
	{
		mat4 tmpMat;
		glm_perspective_lh_zo(45.0f * (3.14f / 180.0f), (float)WindowDetails.WindowWidth / (float)WindowDetails.WindowHeight, 0.1f, 1000.0f, tmpMat);

		glm_mat4_copy(tmpMat, Camera.cameraProjMat);

		vec3 cameraPosition = { 0.0f, 2.0f, -4.0f };

		vec3 cameraTarget = { 0.0f , 0.0f , 0.0f };

		vec3 cameraUp = { 0.0f , 1.0f, 0.0f };

		vec3 cPos;
		glm_vec3_copy(cameraPosition, cPos);

		vec3 cTarg;
		glm_vec3_copy(cameraTarget, cTarg);

		vec3 cUp;
		glm_vec3_copy(cameraUp, cUp);

		glm_lookat_lh(cPos, cTarg, cUp, tmpMat);

		glm_mat4_copy(tmpMat, Camera.cameraViewMat);

		vec3 posVec;
		glm_vec3_copy(Camera.cube1Position, posVec);

		glm_translate_make(tmpMat, posVec);

		glm_mat4_identity(Camera.cube1RotMat);

		glm_mat4_copy(tmpMat, Camera.cube1WorldMat);

		glm_vec3_add(Camera.cube2PositionOffset, Camera.cube1Position, posVec);

		glm_translate_make(tmpMat, posVec);

		glm_mat4_identity(Camera.cube2RotMat);

		glm_mat4_copy(tmpMat, Camera.cube2WorldMat);

		if (DxObjects->RenderTargets[0])
		{
			for (int i = 0; i < BUFFER_COUNT; i++)
			{
				THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->RenderTargets[i]));
				SyncObjects->FenceValue[i] = SyncObjects->FenceValue[SyncObjects->FrameIndex] + 1;
			}
		}

		THROW_ON_FAIL(IDXGISwapChain3_ResizeBuffers(DxObjects->SwapChain, BUFFER_COUNT, WindowDetails.WindowWidth, WindowDetails.WindowHeight, RTV_FORMAT, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));

		SyncObjects->FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects->SwapChain);

		{
			D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = DxObjects->RtvHeapHandle;

			for (int i = 0; i < BUFFER_COUNT; i++)
			{
				THROW_ON_FAIL(IDXGISwapChain3_GetBuffer(DxObjects->SwapChain, i, &IID_ID3D12Resource, &DxObjects->RenderTargets[i]));
				ID3D12Device10_CreateRenderTargetView(Device, DxObjects->RenderTargets[i], NULL, RtvHandle);
				RtvHandle.ptr += DxObjects->RtvDescriptorSize;
			}
		}

		if(DxObjects->DepthStencilBuffer)
		{
			THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->DepthStencilBuffer));
		}

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
			ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			ResourceDesc.Alignment = 0;
			ResourceDesc.Width = WindowDetails.WindowWidth;
			ResourceDesc.Height = WindowDetails.WindowHeight;
			ResourceDesc.DepthOrArraySize = 1;
			ResourceDesc.MipLevels = 1;
			ResourceDesc.Format = DSV_FORMAT;
			ResourceDesc.SampleDesc.Count = 1;
			ResourceDesc.SampleDesc.Quality = 0;
			ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

			D3D12_CLEAR_VALUE ScreenClearValue = { 0 };
			ScreenClearValue.Format = DSV_FORMAT;
			ScreenClearValue.DepthStencil.Depth = 1.0f;
			ScreenClearValue.DepthStencil.Stencil = 0;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, &ScreenClearValue, NULL, 0, NULL, &IID_ID3D12Resource, &DxObjects->DepthStencilBuffer));
		}

#ifdef _DEBUG
		THROW_ON_FAIL(ID3D12Resource_SetName(DxObjects->DepthStencilBuffer, L"Depth/Stencil Buffer"));
#endif

		{
			D3D12_DEPTH_STENCIL_VIEW_DESC DepthStencilViewDesc = { 0 };
			DepthStencilViewDesc.Format = DSV_FORMAT;
			DepthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			DepthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
			ID3D12Device10_CreateDepthStencilView(Device, DxObjects->DepthStencilBuffer, &DepthStencilViewDesc, DxObjects->DsvHeapHandle);
		}
		break;
	}
	case WM_PAINT:
	{
		WaitForPreviousFrame(DxObjects, SyncObjects);

		LARGE_INTEGER tickCountNow;
		QueryPerformanceCounter(&tickCountNow);
		ULONGLONG tickCountDelta = tickCountNow.QuadPart - Timer.tickCount.QuadPart;
		Timer.tickCount.QuadPart = tickCountNow.QuadPart;

		float MovementFactor = (tickCountDelta / ((float)Timer.ProcessorFrequency.QuadPart)) * 75.f;

		mat4 rotXMat;
		glm_mat4_identity(rotXMat);
		glm_rotate_x(rotXMat, 0.01f * MovementFactor, rotXMat);

		mat4 rotYMat;
		glm_mat4_identity(rotYMat);
		glm_rotate_y(rotYMat, 0.02f * MovementFactor, rotYMat);

		mat4 rotZMat;
		glm_mat4_identity(rotZMat);
		glm_rotate_z(rotZMat, 0.03f * MovementFactor, rotZMat);

		mat4 rotMat;
		glm_mat4_mul(Camera.cube1RotMat, rotXMat, rotMat);
		glm_mat4_mul(rotMat, rotYMat, rotMat);
		glm_mat4_mul(rotMat, rotZMat, rotMat);
		glm_mat4_copy(rotMat, Camera.cube1RotMat);

		mat4 translationMat;
		glm_translate_make(translationMat, Camera.cube1Position);

		mat4 worldMat;
		glm_mat4_mul(rotMat, translationMat, worldMat);
		glm_mat4_copy(worldMat, Camera.cube1WorldMat);

		mat4 viewMat;
		glm_mat4_copy(Camera.cameraViewMat, viewMat);

		mat4 projMat;
		glm_mat4_copy(Camera.cameraProjMat, projMat);

		mat4 wvpMat;
		glm_mat4_mul(projMat, viewMat, wvpMat);
		glm_mat4_mul(wvpMat, Camera.cube1WorldMat, wvpMat);

		mat4 ConstantBufferPerObject = { 0 };
		mat4 transposed;
		glm_mat4_transpose_to(wvpMat, transposed);
		glm_mat4_copy(transposed, ConstantBufferPerObject);

		memcpy(DxObjects->ConstantBufferCPUAddress[SyncObjects->FrameIndex], &ConstantBufferPerObject, sizeof(ConstantBufferPerObject));

		glm_mat4_identity(rotXMat);
		glm_rotate_x(rotXMat, 0.03f * MovementFactor, rotXMat);

		glm_mat4_identity(rotYMat);
		glm_rotate_y(rotYMat, 0.02f * MovementFactor, rotYMat);

		glm_mat4_identity(rotZMat);
		glm_rotate_z(rotZMat, 0.01f * MovementFactor, rotZMat);

		glm_mat4_mul(rotZMat, rotYMat, rotMat);
		glm_mat4_mul(rotMat, rotXMat, rotMat);
		glm_mat4_mul(rotMat, Camera.cube2RotMat, rotMat);

		glm_mat4_copy(rotMat, Camera.cube2RotMat);

		mat4 translationOffsetMat;
		glm_translate_make(translationOffsetMat, Camera.cube2PositionOffset);

		mat4 scaleMat;
		const vec3 scaleVec = { 0.5f, 0.5f, 0.5f };
		glm_scale_make(scaleMat, scaleVec);

		glm_mat4_mul(translationMat, rotMat, worldMat);
		glm_mat4_mul(worldMat, translationOffsetMat, worldMat);
		glm_mat4_mul(worldMat, scaleMat, worldMat);

		glm_mat4_mul(projMat, viewMat, wvpMat);
		glm_mat4_mul(wvpMat, Camera.cube2WorldMat, wvpMat);

		glm_mat4_transpose_to(wvpMat, transposed);
		glm_mat4_copy(transposed, ConstantBufferPerObject);

		memcpy(DxObjects->ConstantBufferCPUAddress[SyncObjects->FrameIndex] + ConstantBufferPerObjectAlignedSize, &ConstantBufferPerObject, sizeof(ConstantBufferPerObject));

		glm_mat4_copy(worldMat, Camera.cube2WorldMat);

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(DxObjects->CommandAllocator));
		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Reset(DxObjects->CommandList, DxObjects->CommandAllocator, DxObjects->PipelineStateObject));

		{
			D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
			TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
			TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
			TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
			TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			TextureBarrier.pResource = DxObjects->RenderTargets[SyncObjects->FrameIndex];
			TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 1;
			ResourceBarrier.pTextureBarriers = &TextureBarrier;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->CommandList, 1, &ResourceBarrier);
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = { .ptr = DxObjects->RtvHeapHandle.ptr + (SyncObjects->FrameIndex * DxObjects->RtvDescriptorSize) };

		ID3D12GraphicsCommandList7_OMSetRenderTargets(DxObjects->CommandList, 1, &RtvHandle, FALSE, &DxObjects->DsvHeapHandle);
		ID3D12GraphicsCommandList7_ClearRenderTargetView(DxObjects->CommandList, RtvHandle, ((const float[]) { 0.0f, 0.2f, 0.4f, 1.0f }), 0, NULL);
		ID3D12GraphicsCommandList7_ClearDepthStencilView(DxObjects->CommandList, DxObjects->DsvHeapHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
		
		ID3D12GraphicsCommandList7_SetGraphicsRootSignature(DxObjects->CommandList, DxObjects->RootSignature);

		ID3D12GraphicsCommandList7_SetDescriptorHeaps(DxObjects->CommandList, 1, &DxObjects->SRVDescriptorHeap);

		ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable(DxObjects->CommandList, 1, DxObjects->SrvGpuHandle);
		ID3D12GraphicsCommandList7_RSSetViewports(DxObjects->CommandList, 1, &WindowDetails.Viewport);
		ID3D12GraphicsCommandList7_RSSetScissorRects(DxObjects->CommandList, 1, &WindowDetails.ScissorRect);
		ID3D12GraphicsCommandList7_IASetPrimitiveTopology(DxObjects->CommandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D12GraphicsCommandList7_IASetVertexBuffers(DxObjects->CommandList, 0, 1, &DxObjects->VertexBufferView);
		ID3D12GraphicsCommandList7_IASetIndexBuffer(DxObjects->CommandList, &DxObjects->IndexBufferView);
		ID3D12GraphicsCommandList7_SetGraphicsRootConstantBufferView(DxObjects->CommandList, 0, DxObjects->ContantBufferGPUAddress[SyncObjects->FrameIndex]);
		ID3D12GraphicsCommandList7_DrawIndexedInstanced(DxObjects->CommandList, NUM_CUBE_INDICES, 1, 0, 0, 0);
		ID3D12GraphicsCommandList7_SetGraphicsRootConstantBufferView(DxObjects->CommandList, 0, DxObjects->ContantBufferGPUAddress[SyncObjects->FrameIndex] + ConstantBufferPerObjectAlignedSize);
		ID3D12GraphicsCommandList7_DrawIndexedInstanced(DxObjects->CommandList, NUM_CUBE_INDICES, 1, 0, 0, 0);

		{
			D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
			TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
			TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
			TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
			TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarrier.pResource = DxObjects->RenderTargets[SyncObjects->FrameIndex];
			TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 1;
			ResourceBarrier.pTextureBarriers = &TextureBarrier;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->CommandList, 1, &ResourceBarrier);
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects->CommandList));

		ID3D12CommandQueue_ExecuteCommandLists(DxObjects->CommandQueue, 1, &DxObjects->CommandList);

		THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->CommandQueue, SyncObjects->Fence[SyncObjects->FrameIndex], SyncObjects->FenceValue[SyncObjects->FrameIndex]));

		THROW_ON_FAIL(IDXGISwapChain3_Present(DxObjects->SwapChain, WindowDetails.bVsync ? 1 : 0, WindowDetails.bVsync ? 0 : DXGI_PRESENT_ALLOW_TEARING));
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects, struct SyncObjects* restrict SyncObjects)
{
	SyncObjects->FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects->SwapChain);
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->CommandQueue, SyncObjects->Fence[SyncObjects->FrameIndex], ++SyncObjects->FenceValue[SyncObjects->FrameIndex]));

	if (ID3D12Fence_GetCompletedValue(SyncObjects->Fence[SyncObjects->FrameIndex]) < SyncObjects->FenceValue[SyncObjects->FrameIndex])
	{
		THROW_ON_FAIL(ID3D12Fence_SetEventOnCompletion(SyncObjects->Fence[SyncObjects->FrameIndex], SyncObjects->FenceValue[SyncObjects->FrameIndex], SyncObjects->FenceEvent));
		THROW_ON_FALSE(WaitForSingleObject(SyncObjects->FenceEvent, INFINITE) == WAIT_OBJECT_0);
	}
}
