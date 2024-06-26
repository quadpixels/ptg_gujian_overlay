// Borrowed code from http://www.directxtutorial.com/Lesson.aspx?lessonid=9-1-3
// ptg

#include <windows.h>
#include <stdio.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <assert.h>
#include <string>
#include <vector>
#include <fstream>

#include "..\DLL1\config.h"

HWND g_hwnd;
int WIN_W = 1280, WIN_H = 720; // This is swapchain size; may lead to cropping!
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

LPDIRECT3D9 g_d3d;    // the pointer to our Direct3D interface
LPDIRECT3DDEVICE9 g_d3ddev;    // the pointer to the device class
HMODULE g_hmodule_dll1, g_hmodule_d3d9;
                            // function prototypes
void InitD3D(HWND hWnd);    // sets up and initializes Direct3D
void LoadTexture();
void Render();    // renders a single frame
void CleanD3D();    // closes Direct3D and releases memory
void LoadDLL();

LPD3DXFONT g_font;
std::string g_cmd;

std::vector<std::string> g_texture_paths;
std::vector<LPDIRECT3DTEXTURE9*> g_textures;
std::vector<LPD3DXSPRITE*> g_sprites;
std::vector<std::pair<int, int> > g_imgsizes;
std::vector<D3DXMATRIX> g_sprite_transforms;
int g_texture_idx = 0;
D3DXVECTOR3 g_sprite_pos;

PtgOverlayConfig g_config;

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
    CLEARTYPE_QUALITY, DEFAULT_PITCH, "AR PL UKai CN", &g_font);
  if (FAILED(h)) {
    printf("Failed to create font! %X\n", h);
    assert(0);
  }
}

int RoundUpToPowerOf2(int x) {
  int ret = 1;
  while (ret < x) ret *= 2;
  return ret;
}

void LoadTextureList() {

}

void LoadTexture() {

  const std::string fn = g_config.root_path + "\\SampleScreenList.txt";

  std::ifstream f(fn);
  if (f.good()) {
    while (f.good()) {
      std::string line;
      std::getline(f, line);

      // Lazy load
      g_texture_paths.push_back(g_config.root_path + "\\" + line);
      if (line.empty()) continue;

      int idx1 = int(line.size() - 1), w, h;
      while (line[idx1] != '.' && (idx1 - 1 >= 0)) {
        idx1--;
      }
      int idx0 = idx1 - 1;
      while (true) {
        char c = line[idx0];
        if ((c >= '0' && c <= '9') || c == 'x') idx0--;
        else break;
      }
      idx0++;
      std::string res = line.substr(idx0, idx1 - idx0);
      if (sscanf_s(res.c_str(), "%dx%d", &w, &h) == 2) {
        ;
      }
      else {
        w = 1280; h = 800;
      }
      g_imgsizes.push_back(std::make_pair(w, h));
      printf("[LoadTextureList] [%s] treating as %dx%d\n", line.c_str(), w, h);
      g_textures.push_back(nullptr);
      g_sprites.push_back(nullptr);
      g_sprite_transforms.push_back(D3DXMATRIX());
    }
  }
}

typedef void(*Dll1SetD3D9DevicePtr)(void*);
Dll1SetD3D9DevicePtr g_dll1_setd3d9deviceptr;

typedef void(*Dll1OnPresent)(IDirect3DDevice9*);
Dll1OnPresent g_dll1_onpresent;

typedef void(*SetAudioID)(int);
SetAudioID g_dll1_setaudioid;

typedef void(*DetermineUIScaleFactor)(int, int);
DetermineUIScaleFactor g_dll1_determine_ui_scale;

typedef void(*ShowVideoSubtitles)(const std::string&);
ShowVideoSubtitles g_dll1_showvideosubtitles;

void LoadDLL() {
  while (g_hmodule_dll1 == 0) {
    g_hmodule_dll1 = LoadLibrary(L"ptg_overlay.dll"); // <-- CWD !
    if (g_hmodule_dll1 == 0) {
      printf("LoadLibrary not successful, trying again\n");
      Sleep(1000);
    } else break;
  }
  
  if (g_hmodule_dll1) {
    g_dll1_setd3d9deviceptr = (Dll1SetD3D9DevicePtr)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(2))); // Export number 2
    g_dll1_onpresent = (Dll1OnPresent)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(3)));
    g_dll1_setaudioid = (SetAudioID)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(4)));
    g_dll1_determine_ui_scale = (DetermineUIScaleFactor)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(5)));
    g_dll1_showvideosubtitles = (ShowVideoSubtitles)(GetProcAddress(g_hmodule_dll1, MAKEINTRESOURCEA(6)));

    if (g_dll1_setd3d9deviceptr) {
      g_dll1_setd3d9deviceptr(g_d3ddev);
    }

    printf("[LoadDLL] Dll1's OnPresent = %p\n", g_dll1_onpresent);
  }
  else {
    DWORD last_error = GetLastError();
    printf("[LoadDLL] g_module_dll1 is null, last error = %d(0x%X)\n", last_error, last_error);
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
  g_config.Load();
  InitD3D(g_hwnd);
  LoadTexture();
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
    if (g_cmd == "INTRO") {
      g_dll1_showvideosubtitles("intro");
    }
    else if (g_cmd == "CREDITS") {
      g_dll1_showvideosubtitles("credits");
    }
    else {
      int id = atoi(g_cmd.c_str());
      g_cmd = "";
      g_dll1_setaudioid(id);
    }
    break;
  }
  case VK_LEFT: case VK_UP: {
    g_texture_idx--;
    if (g_texture_idx < 0) g_texture_idx = int(g_sprites.size()) - 1;
    break;
  }
  case VK_RIGHT: case VK_DOWN: {
    g_texture_idx++;
    if (g_texture_idx >= int(g_sprites.size())) { g_texture_idx = 0; }
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
    if (g_texture_idx >= 0 && g_texture_idx < g_sprites.size()) {
      LPD3DXSPRITE *spr = g_sprites[g_texture_idx];
      LPDIRECT3DTEXTURE9 *tex = g_textures[g_texture_idx];
      std::pair<int, int> res = g_imgsizes[g_texture_idx];

      if (spr == nullptr || tex == nullptr) {

        std::string path = g_texture_paths[g_texture_idx];
        tex = new LPDIRECT3DTEXTURE9();
        spr = new LPD3DXSPRITE();

        //LPDIRECT3DTEXTURE9 tex;
        //LPD3DXSPRITE sprite;
        if (SUCCEEDED(D3DXCreateTextureFromFileA(g_d3ddev, path.c_str(), tex))) {
          if (SUCCEEDED(D3DXCreateSprite(g_d3ddev, spr))) {

            g_textures[g_texture_idx] = tex;
            g_sprites[g_texture_idx] = spr;
            const int w = res.first, h = res.second;

            // Set up transformation matrix -- no need to rescale all screenshots to powers-of-two !!
            int w2 = RoundUpToPowerOf2(w), h2 = RoundUpToPowerOf2(h);
            D3DXMATRIX m;
            D3DXVECTOR2 scaling(float(w) / w2, float(h) / h2);
            D3DXVECTOR2 zero(0, 0);
            D3DXMatrixTransformation2D(&m, nullptr, 0.0f, &scaling, &zero, 0.0f, &zero);
            g_sprite_transforms[g_texture_idx] = m;
          }
        }
      }

      g_dll1_determine_ui_scale(res.first, res.second);

      (*spr)->Begin(D3DXSPRITE_ALPHABLEND);
      (*spr)->Draw(*tex, NULL, NULL, &g_sprite_pos, 0xFFFFFFFF);
      (*spr)->SetTransform(&(g_sprite_transforms[g_texture_idx]));
      (*spr)->End();
    }

    RECT rect;
    rect.left = 2;
    rect.top = WIN_H - 100;
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
    g_dll1_onpresent(g_d3ddev);
  }

  g_d3ddev->EndScene();
  g_d3ddev->Present(NULL, NULL, NULL, NULL);
}