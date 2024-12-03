/*
* (C) 2024 badasahog. All Rights Reserved
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

#include <d3d12.h>
#include <dxgi1_6.h>

#include <D3Dcompiler.h>

#include <wincodec.h>

#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <cglm/call.h>
#include <cglm/cam.h>
#include <cglm/clipspace/persp_lh_zo.h>
#include <cglm/clipspace/view_lh.h>

#include <stdint.h>

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:D3DCompiler.lib")
#pragma comment(linker, "/DEFAULTLIB:dxguid.lib")
#pragma comment(linker, "/DEFAULTLIB:windowscodecs.lib")

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

ID3D12Device* Device;

HANDLE ConsoleHandle;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (hr == 0x887A0005)//device removed
	{
		THROW_ON_FAIL_IMPL(ID3D12Device_GetDeviceRemovedReason(Device), line);
	}

	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			nullptr
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, nullptr, nullptr);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, nullptr, nullptr);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, nullptr, nullptr);
			WriteConsoleA(ConsoleHandle, "\n", 1, nullptr, nullptr);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, nullptr, nullptr);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == nullptr || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

inline void MEMCPY_VERIFY_IMPL(errno_t error, int line)
{
	if (error != 0)
	{
		char buffer[28];
		int stringlength = _snprintf_s(buffer, 28, _TRUNCATE, "memcpy failed on line %i\n", line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, nullptr, nullptr);
		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	}
}

#define MEMCPY_VERIFY(x) MEMCPY_VERIFY_IMPL(x, __LINE__)

#define countof(x) (sizeof(x) / sizeof(x[0]))

int WindowWidth = 800;
int WindowHeight = 600;

bool bFullScreen = false;
bool bTearingSupport = false;
bool bVsync = false;

bool Running = true;

#ifdef _DEBUG
ID3D12Debug6* DebugController;
#endif

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#define BUFFER_COUNT 3

LARGE_INTEGER ProcessorFrequency;

IDXGISwapChain3* SwapChain;
ID3D12CommandQueue* CommandQueue;
ID3D12DescriptorHeap* rtvDescriptorHeap;
ID3D12Resource* RenderTargets[BUFFER_COUNT];
ID3D12CommandAllocator* CommandAllocator;
ID3D12GraphicsCommandList* CommandList;
ID3D12Fence* Fence[BUFFER_COUNT];
HANDLE FenceEvent;
UINT64 FenceValue[BUFFER_COUNT];

int FrameIndex;

int rtvDescriptorSize;

void WaitForPreviousFrame();

ID3D12PipelineState* PipelineStateObject;

ID3D12RootSignature* RootSignature;

D3D12_VIEWPORT Viewport = { 0 };

D3D12_RECT ScissorRect = { 0 };

ID3D12Resource* VertexBuffer;
ID3D12Resource* IndexBuffer;

D3D12_VERTEX_BUFFER_VIEW VertexBufferView = { 0 };

D3D12_INDEX_BUFFER_VIEW IndexBufferView = { 0 };

ID3D12Resource* DepthStencilBuffer;
ID3D12DescriptorHeap* dsDescriptorHeap;

struct ConstantBufferPerObject {
	mat4 wvpMat;
};

int ConstantBufferPerObjectAlignedSize = (sizeof(struct ConstantBufferPerObject) + 255) & ~255;

struct ConstantBufferPerObject cbPerObject = { 0 };

ID3D12Resource* ConstantBufferUploadHeaps[BUFFER_COUNT];

UINT8* cbvGPUAddress[BUFFER_COUNT];

int numCubeIndices;

ID3D12Resource* textureBuffer;

ID3D12DescriptorHeap* mainDescriptorHeap;
ID3D12Resource* TextureBufferUploadHeap;

BYTE* imageData;

struct Vertex {
	vec3 pos;
	vec2 texCoord;
};

mat4 cube1RotMat;
vec3 cube1Position = { 0.0f, 0.0f, 0.0f };
mat4 cube1WorldMat;

mat4 cameraViewMat;
mat4 cameraProjMat = { 0 };

mat4 cube2RotMat;
vec3 cube2PositionOffset = { 1.5f, 0.0f, 0.0f };
mat4 cube2WorldMat;

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	HINSTANCE Instance = GetModuleHandleW(nullptr);

	HICON Icon = LoadIconW(nullptr, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(nullptr, IDC_ARROW);
	LPCTSTR WindowClassName = L"MinimalDx12";

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
	WindowClass.lpszMenuName = nullptr;
	WindowClass.lpszClassName = WindowClassName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	HWND Window = CreateWindowExW(
		0,
		WindowClassName,
		L"Minimal DirectX 12",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowWidth,
		WindowHeight,
		nullptr,
		nullptr,
		Instance,
		nullptr);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(QueryPerformanceFrequency(&ProcessorFrequency));

	IDXGIFactory6* Factory;

#ifdef _DEBUG
	THROW_ON_FAIL(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, &IID_IDXGIFactory6, &Factory));
#else
	THROW_ON_FAIL(CreateDXGIFactory2(0, &IID_IDXGIFactory6, &Factory));
#endif

	BOOL allowTearing = FALSE;

	THROW_ON_FAIL(IDXGIFactory6_CheckFeatureSupport(
		Factory,
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&allowTearing,
		sizeof(allowTearing)));

	bTearingSupport = (allowTearing == TRUE);

#ifdef _DEBUG
	ID3D12Debug* DebugControllerV1;
	THROW_ON_FAIL(D3D12GetDebugInterface(&IID_ID3D12Debug, &DebugControllerV1));
	THROW_ON_FAIL(ID3D12Debug_QueryInterface(DebugControllerV1, &IID_ID3D12Debug6, &DebugController));
	ID3D12Debug_Release(DebugControllerV1);

	ID3D12Debug6_SetEnableSynchronizedCommandQueueValidation(DebugController, TRUE);
	ID3D12Debug6_SetForceLegacyBarrierValidation(DebugController, TRUE);
	ID3D12Debug6_SetEnableAutoName(DebugController, TRUE);
	ID3D12Debug6_EnableDebugLayer(DebugController);
	ID3D12Debug6_SetEnableGPUBasedValidation(DebugController, TRUE);
#endif

	IDXGIAdapter1* Adapter;

	IDXGIFactory6_EnumAdapterByGpuPreference(Factory, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, &Adapter);

	THROW_ON_FAIL(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_12_1, &IID_ID3D12Device, &Device));

#ifdef _DEBUG
	ID3D12InfoQueue* InfoQueue;
	THROW_ON_FAIL(ID3D12Device_QueryInterface(Device, &IID_ID3D12InfoQueue, &InfoQueue));

	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE));

	D3D12_MESSAGE_ID MessageIDs[] = {
		D3D12_MESSAGE_ID_DEVICE_CLEARVIEW_EMPTYRECT
	};

	D3D12_INFO_QUEUE_FILTER NewFilter = { 0 };
	NewFilter.DenyList.NumSeverities = 0;
	NewFilter.DenyList.pSeverityList = nullptr;
	NewFilter.DenyList.NumIDs = countof(MessageIDs);
	NewFilter.DenyList.pIDList = MessageIDs;

	THROW_ON_FAIL(ID3D12InfoQueue_PushStorageFilter(InfoQueue, &NewFilter));
#endif

	{
		D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = { 0 };
		CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device_CreateCommandQueue(Device, &CommandQueueDesc, &IID_ID3D12CommandQueue, &CommandQueue));
	}

	{
		DXGI_SWAP_CHAIN_DESC SwapChainDesc = { 0 };
		SwapChainDesc.BufferCount = BUFFER_COUNT;
		SwapChainDesc.BufferDesc.Width = WindowWidth;
		SwapChainDesc.BufferDesc.Height = WindowHeight;
		SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapChainDesc.OutputWindow = Window;
		SwapChainDesc.SampleDesc.Count = 1;
		SwapChainDesc.SampleDesc.Quality = 0;
		SwapChainDesc.Windowed = TRUE;
		SwapChainDesc.Flags = bTearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		IDXGISwapChain* tempSwapChain;

		THROW_ON_FAIL(IDXGIFactory6_CreateSwapChain(Factory, CommandQueue, &SwapChainDesc, &tempSwapChain));

		IDXGISwapChain_QueryInterface(tempSwapChain, &IID_IDXGISwapChain3, &SwapChain);
		IDXGISwapChain_Release(tempSwapChain);
	}

	THROW_ON_FAIL(IDXGIFactory6_MakeWindowAssociation(Factory, Window, DXGI_MWA_NO_ALT_ENTER));
	
	FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(SwapChain);

	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { 0 };
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = BUFFER_COUNT;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device_CreateDescriptorHeap(Device, &rtvHeapDesc, &IID_ID3D12DescriptorHeap, &rtvDescriptorHeap));
	}
	
	rtvDescriptorSize = ID3D12Device_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvDescriptorHeap, &rtvHandle);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(IDXGISwapChain3_GetBuffer(SwapChain, i, &IID_ID3D12Resource, &RenderTargets[i]));

		ID3D12Device_CreateRenderTargetView(Device, RenderTargets[i], nullptr, rtvHandle);

		rtvHandle.ptr += rtvDescriptorSize;
	}

	THROW_ON_FAIL(ID3D12Device_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &CommandAllocator));
	
	THROW_ON_FAIL(ID3D12Device_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, nullptr, &IID_ID3D12GraphicsCommandList, &CommandList));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &Fence[i]));
		FenceValue[i] = 0;
	}

	FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	VALIDATE_HANDLE(FenceEvent);

	D3D12_DESCRIPTOR_RANGE  DescriptorRanges[1] = { 0 };
	DescriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	DescriptorRanges[0].NumDescriptors = 1;
	DescriptorRanges[0].BaseShaderRegister = 0;
	DescriptorRanges[0].RegisterSpace = 0;
	DescriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER  RootParameters[2] = { 0 };
	RootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	RootParameters[0].Descriptor.RegisterSpace = 0;
	RootParameters[0].Descriptor.ShaderRegister = 0;
	RootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	RootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	RootParameters[1].DescriptorTable.NumDescriptorRanges = countof(DescriptorRanges);
	RootParameters[1].DescriptorTable.pDescriptorRanges = &DescriptorRanges[0];
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

	D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { 0 };
	RootSignatureDesc.NumParameters = countof(RootParameters);
	RootSignatureDesc.pParameters = RootParameters;
	RootSignatureDesc.NumStaticSamplers = 1;
	RootSignatureDesc.pStaticSamplers = &Sampler;
	RootSignatureDesc.Flags = 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	{
		ID3D10Blob* errorBuff;
		ID3D10Blob* signature;
		THROW_ON_FAIL(D3D12SerializeRootSignature(&RootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff));

		THROW_ON_FAIL(ID3D12Device_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(signature), ID3D10Blob_GetBufferSize(signature), &IID_ID3D12RootSignature, &RootSignature));
		ID3D10Blob_Release(signature);
	}

	HANDLE VertexShaderFile = CreateFileW(
		L"VertexShader.cso",
		GENERIC_READ,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	VALIDATE_HANDLE(VertexShaderFile);

	SIZE_T VertexShaderSize;

	{
		LARGE_INTEGER tempLongInteger;

		THROW_ON_FALSE(GetFileSizeEx(VertexShaderFile, &tempLongInteger));

		VertexShaderSize = tempLongInteger.QuadPart;
	}

	HANDLE VertexShaderFileMap = CreateFileMappingW(
		VertexShaderFile,
		nullptr,
		PAGE_READONLY,
		0,
		0,
		nullptr);

	VALIDATE_HANDLE(VertexShaderFileMap);

	void* VertexShaderBytecode = MapViewOfFile(VertexShaderFileMap, FILE_MAP_READ, 0, 0, 0);

	HANDLE PixelShaderFile = CreateFileW(
		L"PixelShader.cso",
		GENERIC_READ,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	VALIDATE_HANDLE(PixelShaderFile);

	SIZE_T PixelShaderSize;

	{
		LARGE_INTEGER tempLongInteger;

		THROW_ON_FALSE(GetFileSizeEx(PixelShaderFile, &tempLongInteger));

		PixelShaderSize = tempLongInteger.QuadPart;
	}

	HANDLE PixelShaderFileMap = CreateFileMappingW(
		PixelShaderFile,
		nullptr,
		PAGE_READONLY,
		0,
		0,
		nullptr);

	VALIDATE_HANDLE(PixelShaderFileMap);

	void* PixelShaderBytecode = MapViewOfFile(PixelShaderFileMap, FILE_MAP_READ, 0, 0, 0);

	D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc = { 0 };
	PsoDesc.InputLayout.NumElements = countof(InputElementDesc);
	PsoDesc.InputLayout.pInputElementDescs = InputElementDesc;
	PsoDesc.pRootSignature = RootSignature;
	PsoDesc.VS.BytecodeLength = VertexShaderSize;
	PsoDesc.VS.pShaderBytecode = VertexShaderBytecode;
	PsoDesc.PS.BytecodeLength = PixelShaderSize;
	PsoDesc.PS.pShaderBytecode = PixelShaderBytecode;
	PsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	PsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	PsoDesc.SampleDesc.Count = 1;
	PsoDesc.SampleDesc.Quality = 0;
	PsoDesc.SampleMask = 0xffffffff;
	PsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	PsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	PsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	PsoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	PsoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	PsoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	PsoDesc.RasterizerState.DepthClipEnable = TRUE;
	PsoDesc.RasterizerState.MultisampleEnable = FALSE;
	PsoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
	PsoDesc.RasterizerState.ForcedSampleCount = 0;
	PsoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	PsoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	PsoDesc.BlendState.IndependentBlendEnable = FALSE;
	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
	{
		PsoDesc.BlendState.RenderTarget[i].BlendEnable = FALSE;
		PsoDesc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
		PsoDesc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
		PsoDesc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
		PsoDesc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
		PsoDesc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
		PsoDesc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
		PsoDesc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		PsoDesc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
		PsoDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	PsoDesc.NumRenderTargets = 1;
	PsoDesc.DepthStencilState.DepthEnable = TRUE;
	PsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	PsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	PsoDesc.DepthStencilState.StencilEnable = FALSE;
	PsoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	PsoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	PsoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	PsoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	PsoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	THROW_ON_FAIL(ID3D12Device_CreateGraphicsPipelineState(Device, &PsoDesc, &IID_ID3D12PipelineState, &PipelineStateObject));

	THROW_ON_FALSE(UnmapViewOfFile(VertexShaderBytecode));
	THROW_ON_FALSE(CloseHandle(VertexShaderFileMap));
	THROW_ON_FALSE(CloseHandle(VertexShaderFile));
	
	THROW_ON_FALSE(UnmapViewOfFile(PixelShaderBytecode));
	THROW_ON_FALSE(CloseHandle(PixelShaderFileMap));
	THROW_ON_FALSE(CloseHandle(PixelShaderFile));

	struct Vertex VertexList[] = {
		// front face
		{ -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
		{  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// right side face
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

		// left side face
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// back face
		{  0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f },

		// top face
		{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },

		// bottom face
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f },
	};

	ID3D12Resource* vBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC ResourceDesc = { 0 };
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

		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, &IID_ID3D12Resource, &VertexBuffer));
		ID3D12Resource_SetName(VertexBuffer, L"Vertex Buffer Resource Heap");
		
		D3D12_HEAP_PROPERTIES UploadHeapProperties = { 0 };
		UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		UploadHeapProperties.CreationNodeMask = 1;
		UploadHeapProperties.VisibleNodeMask = 1;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &vBufferUploadHeap));
		ID3D12Resource_SetName(vBufferUploadHeap, L"Vertex Buffer Upload Resource Heap");
		
		void* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(vBufferUploadHeap, 0, nullptr, &pData));

		MEMCPY_VERIFY(memcpy_s(pData, sizeof(VertexList), VertexList, sizeof(VertexList)));
		
		ID3D12Resource_Unmap(vBufferUploadHeap, 0, nullptr);

		ID3D12GraphicsCommandList_CopyBufferRegion(CommandList, VertexBuffer, 0, vBufferUploadHeap, 0, sizeof(VertexList));
	}

	{
		D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		ResourceBarrier.Transition.pResource = VertexBuffer;
		ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		ID3D12GraphicsCommandList_ResourceBarrier(CommandList, 1, &ResourceBarrier);
	}

	// a quad (2 triangles)
	WORD IndexList[] = {
		// front face
		0, 1, 2, // first triangle
		0, 3, 1, // second triangle

		// left face
		4, 5, 6, // first triangle
		4, 7, 5, // second triangle

		// right face
		8, 9, 10, // first triangle
		8, 11, 9, // second triangle

		// back face
		12, 13, 14, // first triangle
		12, 15, 13, // second triangle

		// top face
		16, 17, 18, // first triangle
		16, 19, 17, // second triangle

		// bottom face
		20, 21, 22, // first triangle
		20, 23, 21, // second triangle
	};

	numCubeIndices = countof(IndexList);

	ID3D12Resource* IndexBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC ResourceDesc = { 0 };
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

		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, &IID_ID3D12Resource, &IndexBuffer));
		ID3D12Resource_SetName(IndexBuffer, L"Index Buffer Resource Heap");
		
		D3D12_HEAP_PROPERTIES UploadHeapProperties = { 0 };
		UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		UploadHeapProperties.CreationNodeMask = 1;
		UploadHeapProperties.VisibleNodeMask = 1;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &IndexBufferUploadHeap));
		ID3D12Resource_SetName(IndexBufferUploadHeap, L"Index Buffer Upload Resource Heap");

		void* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(IndexBufferUploadHeap, 0, nullptr, &pData));

		MEMCPY_VERIFY(memcpy_s(pData, sizeof(IndexList), IndexList, sizeof(IndexList)));

		ID3D12Resource_Unmap(IndexBufferUploadHeap, 0, nullptr);

		ID3D12GraphicsCommandList_CopyBufferRegion(CommandList, IndexBuffer, 0, IndexBufferUploadHeap, 0, sizeof(IndexList));
	}
	
	{
		D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		ResourceBarrier.Transition.pResource = IndexBuffer;
		ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
		ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		ID3D12GraphicsCommandList_ResourceBarrier(CommandList, 1, &ResourceBarrier);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { 0 };
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device_CreateDescriptorHeap(Device, &dsvHeapDesc, &IID_ID3D12DescriptorHeap, &dsDescriptorHeap));
	}

	ID3D12DescriptorHeap_SetName(dsDescriptorHeap, L"Depth/Stencil Resource Heap");

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		ResourceDesc.Height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

		D3D12_CLEAR_VALUE ScreenClearValue = { 0 };
		ScreenClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		ScreenClearValue.DepthStencil.Depth = 1.0f;
		ScreenClearValue.DepthStencil.Stencil = 0;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ScreenClearValue, &IID_ID3D12Resource, &DepthStencilBuffer));
	}

	{
		D3D12_DEPTH_STENCIL_VIEW_DESC DepthStencilViewDesc = { 0 };
		DepthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DepthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		DepthStencilViewDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;

		D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsDescriptorHeap, &CpuDescriptorHandle);

		ID3D12Device_CreateDepthStencilView(Device, DepthStencilBuffer, &DepthStencilViewDesc, CpuDescriptorHandle);
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = 1024 * 64;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &ConstantBufferUploadHeaps[i]));

		ID3D12Resource_SetName(ConstantBufferUploadHeaps[i], L"Constant Buffer Upload Resource Heap");

		D3D12_RANGE ReadRange = { 0 };
		ReadRange.Begin = 0;
		ReadRange.End = 0;

		THROW_ON_FAIL(ID3D12Resource_Map(ConstantBufferUploadHeaps[i], 0, &ReadRange, &cbvGPUAddress[i]));

		memcpy(cbvGPUAddress[i], &cbPerObject, sizeof(cbPerObject));
		memcpy(cbvGPUAddress[i] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject));
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = { 0 };
		HeapDesc.NumDescriptors = 1;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		THROW_ON_FAIL(ID3D12Device_CreateDescriptorHeap(Device, &HeapDesc, &IID_ID3D12DescriptorHeap, &mainDescriptorHeap));
	}

	const int textureWidth = 64;
	const int textureHeight = 64;
	const int bytesPerTexel = 2;

	D3D12_RESOURCE_DESC TextureResourceDesc = { 0 };
	TextureResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	TextureResourceDesc.Alignment = 0;
	TextureResourceDesc.Width = textureWidth;
	TextureResourceDesc.Height = textureHeight;
	TextureResourceDesc.DepthOrArraySize = 1;
	TextureResourceDesc.MipLevels = 0;
	TextureResourceDesc.Format = DXGI_FORMAT_B5G6R5_UNORM;
	TextureResourceDesc.SampleDesc.Count = 1;
	TextureResourceDesc.SampleDesc.Quality = 0;
	TextureResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;//todo: strange?
	TextureResourceDesc.Flags = 0;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &TextureResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, &IID_ID3D12Resource, &textureBuffer));
	}

	ID3D12Resource_SetName(textureBuffer, L"Texture Buffer Resource Heap");
	
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperties.CreationNodeMask = 1;
		HeapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = textureWidth * textureHeight * bytesPerTexel;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device_CreateCommittedResource(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &IID_ID3D12Resource, &TextureBufferUploadHeap));
	}
	
	ID3D12Resource_SetName(TextureBufferUploadHeap, L"Texture Buffer Upload Resource Heap");

	{
		WORD* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(TextureBufferUploadHeap, 0, nullptr, &pData));

		for (UINT y = 0; y < textureHeight; y++)
		{
			for (UINT x = 0; x < textureWidth; x++)
			{
				pData[(x * textureWidth + y) * (bytesPerTexel / sizeof(WORD))] = x == 0 || x == 63 || y == 0 || y == 63 ? 0b11111'000000'00000 : rand() * (UINT16_MAX / RAND_MAX);
			}
		}
		
		ID3D12Resource_Unmap(TextureBufferUploadHeap, 0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION TextureCopyDest = { 0 };
		TextureCopyDest.pResource = textureBuffer;
		TextureCopyDest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		TextureCopyDest.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION TextureCopySrc = { 0 };
		TextureCopySrc.pResource = TextureBufferUploadHeap;
		TextureCopySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		TextureCopySrc.PlacedFootprint.Offset = 0;
		TextureCopySrc.PlacedFootprint.Footprint.Format = TextureResourceDesc.Format;
		TextureCopySrc.PlacedFootprint.Footprint.Width = textureWidth;
		TextureCopySrc.PlacedFootprint.Footprint.Height = textureHeight;
		TextureCopySrc.PlacedFootprint.Footprint.Depth = 1;
		TextureCopySrc.PlacedFootprint.Footprint.RowPitch = textureWidth * bytesPerTexel;

		ID3D12GraphicsCommandList_CopyTextureRegion(CommandList, &TextureCopyDest, 0, 0, 0, &TextureCopySrc, nullptr);
	}

	{
		D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		ResourceBarrier.Transition.pResource = textureBuffer;
		ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		ID3D12GraphicsCommandList_ResourceBarrier(CommandList, 1, &ResourceBarrier);
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC ResourceViewDesc = { 0 };
	ResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ResourceViewDesc.Format = TextureResourceDesc.Format;
	ResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	ResourceViewDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle2;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(mainDescriptorHeap, &CpuDescriptorHandle2);

	ID3D12Device_CreateShaderResourceView(Device, textureBuffer, &ResourceViewDesc, CpuDescriptorHandle2);

	ID3D12GraphicsCommandList_Close(CommandList);

	ID3D12CommandList* ppCommandLists[] = { CommandList };
	ID3D12CommandQueue_ExecuteCommandLists(CommandQueue, countof(ppCommandLists), ppCommandLists);

	FenceValue[FrameIndex]++;
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(CommandQueue, Fence[FrameIndex], FenceValue[FrameIndex]));

	VertexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(VertexBuffer);
	VertexBufferView.StrideInBytes = sizeof(struct Vertex);
	VertexBufferView.SizeInBytes = sizeof(VertexList);

	IndexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(IndexBuffer);
	IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	IndexBufferView.SizeInBytes = sizeof(IndexList);

	Viewport.TopLeftX = 0;
	Viewport.TopLeftY = 0;
	Viewport.Width = WindowWidth;
	Viewport.Height = WindowHeight;
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;

	ScissorRect.left = 0;
	ScissorRect.top = 0;
	ScissorRect.right = WindowWidth;
	ScissorRect.bottom = WindowHeight;

	//mathy stuff
	mat4 tmpMat;
	glm_perspective_lh_zo(45.0f * (3.14f / 180.0f), (float)WindowWidth / (float)WindowHeight, 0.1f, 1000.0f, tmpMat);

	glm_mat4_copy(tmpMat, cameraProjMat);

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

	glm_mat4_copy(tmpMat, cameraViewMat);

	vec3 posVec;
	glm_vec3_copy(cube1Position, posVec);

	glm_translate_make(tmpMat, posVec);

	glm_mat4_identity(cube1RotMat);

	glm_mat4_copy(tmpMat, cube1WorldMat);

	glm_vec3_add(cube2PositionOffset, cube1Position, posVec);

	glm_translate_make(tmpMat, posVec);

	glm_mat4_identity(cube2RotMat);

	glm_mat4_copy(tmpMat, cube2WorldMat);


	THROW_ON_FALSE(SetWindowLongPtrA(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	WaitForPreviousFrame();

	for (int i = 0; i < BUFFER_COUNT; i++)
	{

		ID3D12Resource_Unmap(ConstantBufferUploadHeaps[i], 0, nullptr);

		THROW_ON_FAIL(ID3D12Resource_Release(ConstantBufferUploadHeaps[i]));
	}

	THROW_ON_FAIL(ID3D12PipelineState_Release(PipelineStateObject));
	
	THROW_ON_FAIL(ID3D12RootSignature_Release(RootSignature));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(RenderTargets[i]));
	}

	THROW_ON_FAIL(IDXGISwapChain4_Release(SwapChain));

	THROW_ON_FALSE(CloseHandle(FenceEvent));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(Fence[i]));
	}

	THROW_ON_FAIL(ID3D12GraphicsCommandList_Release(CommandList));

	THROW_ON_FAIL(ID3D12CommandAllocator_Release(CommandAllocator));
	
	THROW_ON_FAIL(ID3D12CommandQueue_Release(CommandQueue));

	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(rtvDescriptorHeap));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(dsDescriptorHeap));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12InfoQueue_Release(InfoQueue));
#endif

	THROW_ON_FAIL(ID3D12Device_Release(Device));

	THROW_ON_FAIL(IDXGIAdapter1_Release(Adapter));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Debug6_Release(DebugController));
#endif

	THROW_ON_FAIL(IDXGIFactory6_Release(Factory));

	THROW_ON_FALSE(UnregisterClassW(WindowClassName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

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
		if (!IsIconic(Window))
			THROW_ON_FALSE(SetWindowLongPtrA(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
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
	static LARGE_INTEGER tickCount = { 0 };
	switch (message)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_ESCAPE:
			Running = false;
			THROW_ON_FALSE(DestroyWindow(Window));
			break;
		case 'V':
			bVsync = !bVsync;
			break;
		}
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
		{
			bFullScreen = !bFullScreen;

			if (bFullScreen)
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
		if (IsIconic(Window))
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}
		break;
	case WM_PAINT:
	{
		WaitForPreviousFrame();

		LARGE_INTEGER tickCountNow;
		QueryPerformanceCounter(&tickCountNow);
		ULONGLONG tickCountDelta = tickCountNow.QuadPart - tickCount.QuadPart;
		tickCount.QuadPart = tickCountNow.QuadPart;

		double MovementFactor = (tickCountDelta / ((double)ProcessorFrequency.QuadPart)) * 75.f;

		//more mathy stuff:

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
		glm_mat4_mul(cube1RotMat, rotXMat, rotMat);
		glm_mat4_mul(rotMat, rotYMat, rotMat);
		glm_mat4_mul(rotMat, rotZMat, rotMat);
		glm_mat4_copy(rotMat, cube1RotMat);

		mat4 translationMat;
		glm_translate_make(translationMat, cube1Position);

		mat4 worldMat;
		glm_mat4_mul(rotMat, translationMat, worldMat);
		glm_mat4_copy(worldMat, cube1WorldMat);

		mat4 viewMat;
		glm_mat4_copy(cameraViewMat, viewMat);

		mat4 projMat;
		glm_mat4_copy(cameraProjMat, projMat);

		mat4 wvpMat;
		glm_mat4_mul(projMat, viewMat, wvpMat);
		glm_mat4_mul(wvpMat, cube1WorldMat, wvpMat);

		mat4 transposed;
		glm_mat4_transpose_to(wvpMat, transposed);
		glm_mat4_copy(transposed, cbPerObject.wvpMat);

		memcpy(cbvGPUAddress[FrameIndex], &cbPerObject, sizeof(cbPerObject));

		glm_mat4_identity(rotXMat);
		glm_rotate_x(rotXMat, 0.03f * MovementFactor, rotXMat);

		glm_mat4_identity(rotYMat);
		glm_rotate_y(rotYMat, 0.02f * MovementFactor, rotYMat);

		glm_mat4_identity(rotZMat);
		glm_rotate_z(rotZMat, 0.01f * MovementFactor, rotZMat);

		glm_mat4_mul(rotZMat, rotYMat, rotMat);
		glm_mat4_mul(rotMat, rotXMat, rotMat);
		glm_mat4_mul(rotMat, cube2RotMat, rotMat);

		glm_mat4_copy(rotMat, cube2RotMat);

		mat4 translationOffsetMat;
		glm_translate_make(translationOffsetMat, cube2PositionOffset);

		mat4 scaleMat;
		vec3 scaleVec = { 0.5f, 0.5f, 0.5f };
		glm_scale_make(scaleMat, scaleVec);

		glm_mat4_mul(translationMat, rotMat, worldMat);
		glm_mat4_mul(worldMat, translationOffsetMat, worldMat);
		glm_mat4_mul(worldMat, scaleMat, worldMat);

		glm_mat4_mul(projMat, viewMat, wvpMat);
		glm_mat4_mul(wvpMat, cube2WorldMat, wvpMat);

		glm_mat4_transpose_to(wvpMat, transposed);
		glm_mat4_copy(transposed, cbPerObject.wvpMat);

		memcpy(cbvGPUAddress[FrameIndex] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject));

		glm_mat4_copy(worldMat, cube2WorldMat);

		WaitForPreviousFrame();

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(CommandAllocator));

		THROW_ON_FAIL(ID3D12GraphicsCommandList_Reset(CommandList, CommandAllocator, PipelineStateObject));

		{
			D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			ResourceBarrier.Transition.pResource = RenderTargets[FrameIndex];
			ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			ID3D12GraphicsCommandList_ResourceBarrier(CommandList, 1, &ResourceBarrier);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { 0 };

		D3D12_CPU_DESCRIPTOR_HANDLE HeapStart;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvDescriptorHeap, &HeapStart);

		rtvHandle.ptr = HeapStart.ptr + (FrameIndex * rtvDescriptorSize);

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsDescriptorHeap, &dsvHandle);

		ID3D12GraphicsCommandList_OMSetRenderTargets(CommandList, 1, &rtvHandle, FALSE, &dsvHandle);

		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		ID3D12GraphicsCommandList_ClearRenderTargetView(CommandList, rtvHandle, clearColor, 0, nullptr);

		D3D12_CPU_DESCRIPTOR_HANDLE CpuDescHandle;

		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsDescriptorHeap, &CpuDescHandle);

		ID3D12GraphicsCommandList_ClearDepthStencilView(CommandList, CpuDescHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		ID3D12GraphicsCommandList_SetGraphicsRootSignature(CommandList, RootSignature);

		ID3D12DescriptorHeap* descriptorHeaps[] = { mainDescriptorHeap };
		ID3D12GraphicsCommandList_SetDescriptorHeaps(CommandList, countof(descriptorHeaps), descriptorHeaps);

		D3D12_GPU_DESCRIPTOR_HANDLE GpuDescHandle;
		ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(mainDescriptorHeap, &GpuDescHandle);
		ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(CommandList, 1, GpuDescHandle);
		ID3D12GraphicsCommandList_RSSetViewports(CommandList, 1, &Viewport);
		ID3D12GraphicsCommandList_RSSetScissorRects(CommandList, 1, &ScissorRect);
		ID3D12GraphicsCommandList_IASetPrimitiveTopology(CommandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D12GraphicsCommandList_IASetVertexBuffers(CommandList, 0, 1, &VertexBufferView);
		ID3D12GraphicsCommandList_IASetIndexBuffer(CommandList, &IndexBufferView);
		ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(CommandList, 0, ID3D12Resource_GetGPUVirtualAddress(ConstantBufferUploadHeaps[FrameIndex]));
		ID3D12GraphicsCommandList_DrawIndexedInstanced(CommandList, numCubeIndices, 1, 0, 0, 0);
		ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(CommandList, 0, ID3D12Resource_GetGPUVirtualAddress(ConstantBufferUploadHeaps[FrameIndex]) + ConstantBufferPerObjectAlignedSize);
		ID3D12GraphicsCommandList_DrawIndexedInstanced(CommandList, numCubeIndices, 1, 0, 0, 0);

		{
			D3D12_RESOURCE_BARRIER ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			ResourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			ResourceBarrier.Transition.pResource = RenderTargets[FrameIndex];
			ResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			ResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			ResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			ID3D12GraphicsCommandList_ResourceBarrier(CommandList, 1, &ResourceBarrier);
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList_Close(CommandList));

		ID3D12CommandList* ppCommandLists[] = { CommandList };

		ID3D12CommandQueue_ExecuteCommandLists(CommandQueue, countof(ppCommandLists), ppCommandLists);

		THROW_ON_FAIL(ID3D12CommandQueue_Signal(CommandQueue, Fence[FrameIndex], FenceValue[FrameIndex]));

		THROW_ON_FAIL(IDXGISwapChain3_Present(SwapChain, bVsync ? 1 : 0, (bTearingSupport && !bVsync) ? DXGI_PRESENT_ALLOW_TEARING : 0));
		break;
	}
	case WM_DESTROY:
		Running = false;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

void WaitForPreviousFrame()
{
	FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(SwapChain);
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(CommandQueue, Fence[FrameIndex], ++FenceValue[FrameIndex]));

	if (ID3D12Fence_GetCompletedValue(Fence[FrameIndex]) < FenceValue[FrameIndex])
	{
		if (FAILED(ID3D12Fence_SetEventOnCompletion(Fence[FrameIndex], FenceValue[FrameIndex], FenceEvent)))
		{
			Running = false;
		}

		WaitForSingleObject(FenceEvent, INFINITE);
	}
}
