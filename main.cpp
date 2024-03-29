#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "d3d11.lib") // Add this line
#pragma comment(lib, "d3dx11.lib")
#include <Windows.h>

#include <SDK/Vendor/ImGui/imgui.h>
#include <SDK/Vendor/ImGui/imgui_impl_dx11.h>
#include <SDK/Vendor/ImGui/imgui_impl_win32.h>

#include <SDK/Memory/globals.hpp>
#include <D3DX11tex.h>
#include <d3dx11.h>
#include <dwmapi.h>
#include <d3d11.h> // This is where D3D11CreateDeviceAndSwapChain is declared
#include <thread>
#include <Modules/offsets.hpp>
#include <Modules/Features/Caching/player_cache.h>
#include <Modules/Features/Visual/sense.h>
#include <iostream>


/* AIMBOT DEFINATIONS */
int screenWeight = 1920; // In-game resolution
int screenHeight = 1080;
int xFOV = 200; //Aimbot horizontal FOV (square)
int yFOV = 200; //Aimbot vertical FOV (square)
int aSmoothAmount = 1; // Aimbot smoothness

int crosshairX = screenWeight / 2;
int crosshairY = screenHeight / 2;
int entX = 0;
int entY = 0;
int closestX = 0;
int closestY = 0;
int aX = 0;
int aY = 0;
float entNewVisTime = 0;
float entOldVisTime[100];
int visCooldownTime[100];

struct Vector3 {
	float x, y, z;
};

struct Matrix {
	float matrix[16];
};

#define CHECK_VALID( _v ) 0
#define Assert( _exp ) ((void)0)

#define FastSqrt(x)			(sqrt)(x)

#define M_PI 3.14159265358979323846264338327950288419716939937510
#define M_PI_F		((float)(M_PI))	// Shouldn't collide with anything.
#define RAD2DEG( x  )  ( (float)(x) * (float)(180.f / M_PI_F) )
#define DEG2RAD( x  )  ( (float)(x) * (float)(M_PI_F / 180.f) )


struct matrix3x4_t
{
	matrix3x4_t() {}
	matrix3x4_t(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23)
	{
		m_flMatVal[0][0] = m00;	m_flMatVal[0][1] = m01; m_flMatVal[0][2] = m02; m_flMatVal[0][3] = m03;
		m_flMatVal[1][0] = m10;	m_flMatVal[1][1] = m11; m_flMatVal[1][2] = m12; m_flMatVal[1][3] = m13;
		m_flMatVal[2][0] = m20;	m_flMatVal[2][1] = m21; m_flMatVal[2][2] = m22; m_flMatVal[2][3] = m23;
	}

	float* operator[](int i) { Assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	const float* operator[](int i) const { Assert((i >= 0) && (i < 3)); return m_flMatVal[i]; }
	float* Base() { return &m_flMatVal[0][0]; }
	const float* Base() const { return &m_flMatVal[0][0]; }

	float m_flMatVal[3][4];
};
Vector3 GetEntityBasePosition(uintptr_t ent)
{
	return typenull::memory->read<Vector3>(ent + OFFSET_ORIGIN);
}
Vector3 getBonePosition(int id, uintptr_t ent)
{
	Vector3 origin = GetEntityBasePosition(ent);

	//BoneByHitBox
	uint64_t Model = typenull::memory->read<uint64_t>(ent + OFFSET_STUDIOHDR);

	//get studio hdr
	uint64_t StudioHdr = typenull::memory->read<uint64_t>(Model + 0x8);


	//get hitbox array
	uint16_t HitboxCache = typenull::memory->read<uint16_t>(StudioHdr + 0x34);

	uint64_t HitBoxsArray = StudioHdr + ((uint16_t)(HitboxCache & 0xFFFE) << (4 * (HitboxCache & 1)));

	uint16_t IndexCache = typenull::memory->read<uint16_t>(HitBoxsArray + 0x4);

	int HitboxIndex = ((uint16_t)(IndexCache & 0xFFFE) << (4 * (IndexCache & 1)));
	uint16_t Bone = typenull::memory->read<uint16_t>(HitBoxsArray + HitboxIndex + (id * 0x20));


	if (Bone < 0 || Bone > 255)
		return Vector3();

	//hitpos
	uint64_t BoneArray = typenull::memory->read<uint64_t>(ent + OFFSET_BONES);

	matrix3x4_t Matrix = typenull::memory->read<matrix3x4_t>(BoneArray + Bone * sizeof(matrix3x4_t));
	return Vector3(Matrix.m_flMatVal[0][3] + origin.x, Matrix.m_flMatVal[1][3] + origin.y, Matrix.m_flMatVal[2][3] + origin.z);
}

struct Vector3 _WorldToScreen(const struct Vector3 pos, struct Matrix matrix) {
	struct Vector3 out;
	float _x = matrix.matrix[0] * pos.x + matrix.matrix[1] * pos.y + matrix.matrix[2] * pos.z + matrix.matrix[3];
	float _y = matrix.matrix[4] * pos.x + matrix.matrix[5] * pos.y + matrix.matrix[6] * pos.z + matrix.matrix[7];
	out.z = matrix.matrix[12] * pos.x + matrix.matrix[13] * pos.y + matrix.matrix[14] * pos.z + matrix.matrix[15];

	_x *= 1.f / out.z;
	_y *= 1.f / out.z;

	int width = screenWeight;
	int height = screenHeight;

	out.x = width * .5f;
	out.y = height * .5f;

	out.x += 0.5f * _x * width + 0.5f;
	out.y -= 0.5f * _y * height + 0.5f;

	return out;
}

static float oVisTime[100];

float LastVisTime(uintptr_t ent)
{
	return typenull::memory->read<float>(ent + OFFSET_VISIBLE_TIME);
}

bool IsVisible(uintptr_t ent, int i) {
	const auto VisCheck = LastVisTime(ent);

	const auto IsVis = VisCheck > oVisTime[i] || VisCheck < 0.f && oVisTime[i] > 0.f;

	oVisTime[i] = VisCheck;

	return IsVis;
}


bool glowActive = true;

auto run_core() -> void {

	if (CachedPlayerList.empty()) return;
	uint64_t localent = typenull::memory->read<uint64_t>(typenull::c_base + OFFSET_LocalPlayer);
	if (localent == 0) return;
	for (auto& entity : CachedPlayerList) {

		int health = typenull::memory->read<int>(entity.player_entity + OFFSET_HEALTH);
		int shield = typenull::memory->read<int>(entity.player_entity + OFFSET_SHIELD);

		Vector3 FeetPosition = GetEntityBasePosition(entity.player_entity);
		Vector3 HeadPosition = getBonePosition(0, entity.player_entity);

		if(glowActive)
			Sense::runGlow(entity.player_entity, localent, 1);

		uint64_t viewRenderer = typenull::memory->read<uint64_t>(typenull::c_base + OFFSET_VIEWRENDER);
		uint64_t viewMatrix = typenull::memory->read<uint64_t>(viewRenderer + OFFSET_MATRIX);
		Matrix m = typenull::memory->read<Matrix>(viewMatrix);
		Vector3 w2sHeadAimPos = _WorldToScreen(HeadPosition, m);

		if (abs(crosshairX - entX) < abs(crosshairX - closestX) && abs(crosshairX - entX) < xFOV && abs(crosshairY - entY) < abs(crosshairY - closestY) && abs(crosshairY - entY) < yFOV)
		{
			// Aimbot find closest target
			closestX = entX;
			closestY = entY;
		}

		if (closestX != 9999 && closestY != 9999)
		{
			// If aimbot key pressed
			if (GetAsyncKeyState(VK_LBUTTON) || GetAsyncKeyState(VK_RBUTTON))
			{
				// If mouse cursor shown
				CURSORINFO ci = { sizeof(CURSORINFO) };
				if (GetCursorInfo(&ci))
				{
					if (ci.flags == 0)
						aX = (closestX - crosshairX) / aSmoothAmount;
					aY = (closestY - crosshairY) / aSmoothAmount;
					mouse_event(MOUSEEVENTF_MOVE, aX, aY, 0, 0); // enable aimbot when mouse cursor is hidden
				}
			}
		}
	}
}

bool isMenuOpen = true;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ChangeClickability(bool canclick, HWND hwnd) {
	LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);

	if (canclick) {
		// For clickable remove WS_EX_LAYERED ve WS_EX_TRANSPARENT 
		style &= ~WS_EX_LAYERED;
		style &= ~WS_EX_TRANSPARENT;
	}
	else {
		// non clickable add this ones.
		style |= WS_EX_LAYERED;
		style |= WS_EX_TRANSPARENT;
	}

	SetWindowLong(hwnd, GWL_EXSTYLE, style);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
	if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param)) {
		return 0L;
	}

	if (message == WM_DESTROY) {
		PostQuitMessage(0);
		return 0L;
	}

	return DefWindowProc(window, message, w_param, l_param);
}

INT APIENTRY WinMain(HINSTANCE instance, HINSTANCE, PSTR, INT cmd_show) {

	AllocConsole();	freopen("CONOUT$", "w", stdout);


	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = window_procedure;
	wc.hInstance = instance;
	wc.lpszClassName = L"OBS";

	RegisterClassExW(&wc);

	const HWND window = CreateWindowExW(
		WS_EX_TOPMOST,
		wc.lpszClassName,
		L"Overlay",
		WS_POPUP,
		0, 0,
		1920, 1080,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);
	SetLayeredWindowAttributes(window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA);
	{
		RECT client_area{};
		GetClientRect(window, &client_area);

		RECT window_area{};
		GetWindowRect(window, &window_area);

		POINT diff{};
		ClientToScreen(window, &diff);

		const MARGINS margins{
			window_area.left + (diff.x - window_area.left),
			window_area.top + (diff.y - window_area.top),
			client_area.right,
			client_area.bottom
		};

		DwmExtendFrameIntoClientArea(window, &margins);
	}

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferDesc.RefreshRate.Numerator = 60U;
	sd.BufferDesc.RefreshRate.Denominator = 10;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.SampleDesc.Count = 1U;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2U;
	sd.OutputWindow = window;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


	constexpr D3D_FEATURE_LEVEL levels[2]{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};

	ID3D11Device* device{ nullptr };
	ID3D11DeviceContext* device_context{ nullptr };
	IDXGISwapChain* swap_chain{ nullptr };
	ID3D11RenderTargetView* render_target_view{ nullptr };
	D3D_FEATURE_LEVEL level{};

	D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0U,
		levels,
		2U,
		D3D11_SDK_VERSION,
		&sd,
		&swap_chain,
		&device,
		&level,
		&device_context
	);

	ID3D11Texture2D* back_buffer{ nullptr };
	swap_chain->GetBuffer(0U, IID_PPV_ARGS(&back_buffer));

	if (back_buffer) {
		device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view);
		back_buffer->Release();
	}
	else { return 1; }

	ShowWindow(window, cmd_show);
	UpdateWindow(window);

	ImGui::CreateContext();
	ImGui::StyleColorsClassic();

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(device, device_context);

	bool running = true;

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImFontConfig font_cfg;
	font_cfg.OversampleH = 1;
	font_cfg.OversampleV = 1;
	font_cfg.PixelSnapH = true;

	ID3D11ShaderResourceView* Image = nullptr;
	D3DX11_IMAGE_LOAD_INFO info;
	ID3DX11ThreadPump* pump{ nullptr };
	bool firstart = true;

	while (running) {

		MSG msg;
		while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				running = false;
			}
		}
		if (!running) {
			break;
		}
		if (GetAsyncKeyState(VK_INSERT) & 1) {
			isMenuOpen = !isMenuOpen;
			ChangeClickability(isMenuOpen, window);
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();

		// renders here
		{

			if (firstart) {

				typenull::memory = std::make_unique<memory_typenull>("6NQ36anSqanfa", ("r5apex.exe"));
				typenull::c_base = typenull::memory->base_address();
				std::cout << "c_base" << typenull::c_base;
				std::thread threadObj2(PlayerCache::updateCache);
				threadObj2.detach();
				Sleep(1500);

				firstart = false;

			}
			if (isMenuOpen) {
				ImGui::SetNextWindowSize(ImVec2(400, 450)); // window UI size

				ImGui::Begin("##", &isMenuOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
				ImGui::Text("typenull");
				ImGui::End();
			}
			ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(screenWeight / 2, screenHeight / 2), xFOV, IM_COL32(255, 0, 0, 255), 100, 1.0f);

			run_core();
		}



		ImGui::Render();

		constexpr float color[4]{ 0.f,0.f,0.f,0.f };
		device_context->OMSetRenderTargets(1U, &render_target_view, nullptr);
		device_context->ClearRenderTargetView(render_target_view, color);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		//VSYNC TURN OFF FOR LAG.
		swap_chain->Present(0U, 0U);

	}


	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();

	ImGui::DestroyContext();

	if (swap_chain) {
		swap_chain->Release();
	}

	if (device_context) {
		device_context->Release();
	}

	if (device) {
		device->Release();
	}

	if (render_target_view) {
		render_target_view->Release();
	}

	DestroyWindow(window);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	return 0;

}
