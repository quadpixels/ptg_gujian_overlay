// Borrowed code from http://www.directxtutorial.com/Lesson.aspx?lessonid=9-1-3
// ptg

#include <windows.h>
#include <stdio.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <assert.h>
#include <string>

HWND g_hwnd;
int WIN_W = 1024, WIN_H = 200;
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

LPDIRECT3D9 g_d3d;    // the pointer to our Direct3D interface
LPDIRECT3DDEVICE9 g_d3ddev;    // the pointer to the device class
HMODULE g_hmodule_dll1, g_hmodule_d3d9;

                            // function prototypes
void InitD3D(HWND hWnd);    // sets up and initializes Direct3D
void Render();    // renders a single frame
void CleanD3D();    // closes Direct3D and releases memory
void LoadDLL();

LPD3DXFONT g_font;
std::string g_cmd;

void InitD3D(HWND hwnd) {
  g_d3d = Direct3DCreate9(D3D_SDK_VERSION);    // create the Direct3D interface

  D3DPRESENT_PARAMETERS d3dpp;    // create a struct to hold various device information

  ZeroMemory(&d3dpp, sizeof(d3dpp));        // clear out the struct for use
  d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
  d3dpp.Windowed = TRUE;                    // program windowed, not fullscreen
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; // discard old frames
  d3dpp.hDeviceWindow = hwnd;    // set the window to be used by Direct3D
                                 // create a device class using this information and information from the d3dpp stuct
  HRESULT h = g_d3d->CreateDevice(D3DADAPTER_DEFAULT,
    D3DDEVTYPE_HAL,
    hwnd,
    D3DCREATE_SOFTWARE_VERTEXPROCESSING,
    &d3dpp,
    &g_d3ddev);

  if (FAILED(h)) {
    printf("Failed! result=%X\n", h);
    assert(0);
  }

  h = D3DXCreateFontA(g_d3ddev, 18, 0, 0, 0, false, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS,
    CLEARTYPE_QUALITY, DEFAULT_PITCH, "KaiTi", &g_font);
  if (FAILED(h)) {
    printf("Failed to create font! %X\n", h);
    assert(0);
  }
}

typedef void(*Dll1SetD3D9DevicePtr)(void*);
Dll1SetD3D9DevicePtr g_dll1_setd3d9deviceptr;

typedef void(*Dll1OnPresent)();
Dll1OnPresent g_dll1_onpresent;

typedef void(*SetAudioID)(int);
SetAudioID g_dll1_setaudioid;

void LoadDLL() {
  g_hmodule_dll1 = LoadLibrary(L"Dll1.dll"); // <-- CWD !
  
  if (g_hmodule_dll1) {
    g_dll1_setd3d9deviceptr = (Dll1SetD3D9DevicePtr)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(2))); // Export number 2
    g_dll1_onpresent = (Dll1OnPresent)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(3)));
    g_dll1_setaudioid = (SetAudioID)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(4)));

    if (g_dll1_setd3d9deviceptr) {
      g_dll1_setd3d9deviceptr(g_d3ddev);
    }

    printf("Dll1's OnPresent = %p\n", g_dll1_onpresent);
  }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
  AllocConsole();
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

  printf("_WIN32 = %X\n", _WIN32);
  printf("_WIN32_WINNT = %X\n", _WIN32_WINNT);

  // Create Window
  WNDCLASSEX wc = { 0 };
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = L"DX9 Testbed";
  RegisterClassEx(&wc);

  RECT rect = { 0, 0, WIN_W, WIN_H };
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindow(
    wc.lpszClassName,
    L"DX9 Testbed",
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    WIN_W, WIN_H,
    nullptr,
    nullptr,
    hInstance,
    nullptr
  );

  // INITIALIZE
  InitD3D(g_hwnd);
  LoadDLL();

  ShowWindow(g_hwnd, nCmdShow);

  // Main message loop
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}

void OnKeyDown(WPARAM wParam, LPARAM lParam) {
  if (lParam & 0x40000000) return;
  switch (wParam) {
  case VK_ESCAPE:
    PostQuitMessage(0);
    break;
  case VK_BACK:
    if (g_cmd.empty() == false)
      g_cmd.pop_back();
    printf("CMD: %s\n", g_cmd.c_str());
    break;
  case VK_RETURN: {
    int id = atoi(g_cmd.c_str());
    g_cmd = "";
    g_dll1_setaudioid(id);
    break;
  }
  default: {
    if ((wParam >= 'A' && wParam <= 'Z') ||
      (wParam >= '0' && wParam <= '9')) {
      g_cmd.push_back(char(wParam));
      printf("CMD: %s\n", g_cmd.c_str());
    }

    break;
  }
  }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
  {
    LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    SetWindowLongPtr(g_hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    break;
  }
  case WM_KEYDOWN:
    OnKeyDown(wParam, lParam);
    return 0;
  case WM_KEYUP:
    return 0;
  case WM_PAINT:
    Render();
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

void Render() {
  if (g_d3ddev == nullptr) return;
  g_d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0x33, 0x33, 0x33), 1.0f, 0);
  g_d3ddev->BeginScene();
  
  // Testbed content
  {
    RECT rect;
    rect.left = 2;
    rect.top = 130;
    rect.right = 200;
    rect.bottom = WIN_H;

    RECT rect_cmd;

    // Measure text width
    g_font->DrawTextA(NULL, g_cmd.c_str(), int(g_cmd.size()), &rect_cmd, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    g_font->DrawTextA(NULL, g_cmd.c_str(), int(g_cmd.size()), &rect, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 255, 255));

    rect.left += rect_cmd.right - rect_cmd.left;
    g_font->DrawTextA(NULL, "_", 1, &rect, DT_NOCLIP, D3DCOLOR_ARGB(255, 0, 255, 0));
  }

  // Overlay DLL
  if (g_dll1_onpresent) {
    g_dll1_onpresent();
  }

  g_d3ddev->EndScene();
  g_d3ddev->Present(NULL, NULL, NULL, NULL);
}