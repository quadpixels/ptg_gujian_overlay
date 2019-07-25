// dllmain.cpp : Defines the entry point for the DLL application.

#include <stdio.h>
#include "gs_sdk.h"
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#include <locale.h>
#include <d3d9.h>
#include <d3dx9.h> // Needs Legacy DX SDK
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <codecvt>
#include <set>
#include <vector>
#include <WinBase.h>
#include <tchar.h>
#include "miniz.h"
#include <algorithm>
#include <cctype>

#include "config.h"

mz_zip_archive g_archive_inverted_index;
mz_zip_archive g_archive_dialog_aligned;
void DetermineUIScaleFactor(const int w, const int h);

// For global use
IDirect3DDevice9* g_pD3DDevice = NULL;
void* g_hook554 = NULL;
IDirect3DSwapChain9* g_pSwapchain = NULL;
IDirect3DSurface9* g_pSurfaceCopy = NULL; // For copy
LPD3DXBUFFER g_pSurfaceBuf = NULL;

extern IDirect3DVertexBuffer9* g_vert_buf;
extern IDirect3DStateBlock9* g_pStateBlock;
bool g_screenshot_flag;
CRITICAL_SECTION g_critsect_rectlist;
extern volatile bool g_detector_sleeping;

extern void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor);
extern void WakeDetectorThread(unsigned char* ch, const int len, const int mode);
extern void DrawHighlightRects();
extern void DetermineUIScaleFactor(const int w, const int h);

// https://gamedev.stackexchange.com/questions/26759/best-way-to-get-elapsed-time-in-miliseconds-in-windows?utm_medium=organic&utm_source=google_rich_qa&utm_campaign=google_rich_qa
long long MillisecondsNow() {
  static LARGE_INTEGER s_frequency;
  static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
  if (s_use_qpc) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (1000LL * now.QuadPart) / s_frequency.QuadPart;
  }
  else {
    return GetTickCount();
  }
}

int g_mouse_x, g_mouse_y;
void Update();
HWND g_hwnd;

PtgOverlayConfig g_config;

//========================
// Our overlay stuff ...
std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > g_converter;

int FONT_SIZE = 18;
LPD3DXFONT g_font;
int g_frame_count = 0;

#include "ClosedCaption.h"

MyMessageBox* g_hover_messagebox;

std::string ClosedCaption::alignment_path = "";
ClosedCaption g_ClosedCaption;

void LoadCaptions() {
  std::string p = ClosedCaption::alignment_path;

#if 0
  char fn[266];
  for (int q = 1; q <= 17; q++) {
    sprintf_s(fn, "%s\\inverted_index\\Q%02d.txt", p.c_str(), q);
    g_ClosedCaption.LoadIndex(fn);
  }

  for (int m = 1; m <= 19; m++) {
    sprintf_s(fn, "%s\\inverted_index\\M%02d.txt", p.c_str(), m);
    g_ClosedCaption.LoadIndex(fn);
  }
#endif

  // Inverted Index
  {
    char fn[266];
    sprintf_s(fn, "%s\\inv_idx_zipped.gz", p.c_str());
    mz_bool ret = mz_zip_reader_init_file(&g_archive_inverted_index, fn, 0);
    if (ret == 0) {
      printf("Could not open %s\n", fn);
      return;
    }

    // Extract from tgz file
    for (int q = 1; q <= 17; q++) {
      sprintf_s(fn, "Q%02d.txt", q);
      size_t sz;
      void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_inverted_index, fn, &sz, 0);
      printf("%s = %u bytes\n", fn, unsigned(sz));
      g_ClosedCaption.LoadIndexFromMemory(buf, int(sz));
      delete[] buf;
    }
    for (int m = 1; m <= 19; m++) {
      sprintf_s(fn, "M%02d.txt", m);
      size_t sz;
      void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_inverted_index, fn, &sz, 0);
      printf("%s = %u bytes\n", fn, unsigned(sz));
      g_ClosedCaption.LoadIndexFromMemory(buf, int(sz));
      delete[] buf;
    }
  }


  {
    char fn[256];
    sprintf_s(fn, "%s\\dialog_aligned.gz", p.c_str());
    mz_bool ret = mz_zip_reader_init_file(&g_archive_dialog_aligned, fn, 0);
    if (ret == 0) {
      printf("Could not open %s\n", fn);
    }
  }

  if (true) {
    g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\M01_Rev.txt").c_str());
    g_ClosedCaption.LoadProperNamesList((g_config.root_path + "\\ProperNames.txt").c_str());
  }
}

void LoadImagesAndInitDetectThread();

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
  FILE* f = NULL;

  switch (ul_reason_for_call)
  {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }
  return TRUE;
}

//LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
typedef LRESULT (CALLBACK *WndProcType)(HWND, UINT, WPARAM, LPARAM);
WndProcType g_wndproc_orig;

PHOOK_FUNCTION_TABLE g_pTable;

typedef int(__thiscall *GetStoryValue)(void* This, char const* x);
GetStoryValue g_f2209_orig;
void* g_TheCollection = NULL;
int GetStory(char const * x) {
  if (!g_TheCollection) return -999;
  return g_f2209_orig(g_TheCollection, x);
}

// 新功能：需要手动word wrap（使得单词在换行时保持完整）
void* g_hook2193 = NULL;
typedef int(*FuncTalk)(char const*, char const*, bool, bool, bool);
FuncTalk g_f2193_orig;
int hooked_f2193(char const* arg0, char const* arg1, bool b1, bool b2, bool b3) {
  printf(">> hooked_f2193\n");
  // Word Wrap!
  const int LINE_WIDTH = 72; // 72 chars
  // Convert to wchar_t string for processing
  int len_dialog = MultiByteToWideChar(CP_UTF8, 0, arg1, -1, NULL, NULL);
  printf("   len_dialog=%d\n", len_dialog);
  wchar_t* wterm = new wchar_t[len_dialog];
  MultiByteToWideChar(CP_UTF8, 0, arg1, -1, wterm, len_dialog);
    
  std::wstring wrapped;
  std::wstring curr_word;
  int pos = 0;
  for (int i = 0; i <= len_dialog; i++) {
    printf("%d ", i);
    fflush(stdout);
    if (i == len_dialog || (i < len_dialog && wterm[i] == ' ')) {
      if (pos + curr_word.size() > LINE_WIDTH) {
        for (int p = pos; p < LINE_WIDTH; p++) {
          wrapped.push_back(L' ');
        }
        pos = 0;
      }

      pos += curr_word.size();
      wrapped += curr_word;

      pos += 1;
      wrapped += L' ';

      if (pos >= LINE_WIDTH) {
        if (i < len_dialog) {
          pos = 0;
        }
      }
      curr_word = L"";
    }
    else if (i < len_dialog) {
      curr_word.push_back(wterm[i]);
    }
  }
  printf("\n");

  // Convert back to MB string for passing into the original function
  int len_dialog_mb = WideCharToMultiByte(CP_UTF8, 0, wrapped.c_str(), int(wrapped.size()), NULL, NULL, "", FALSE);
  printf("len_dialog_mb = %d\n", len_dialog_mb);
  char* wrapped_dialog = new char[len_dialog_mb];
  WideCharToMultiByte(CP_UTF8, 0, wrapped.c_str(), int(wrapped.size()), wrapped_dialog, len_dialog_mb, "", FALSE);

  // HOOK
  g_pTable->DoInlineHook(g_hook2193, false);
  int ret = g_f2193_orig(arg0, wrapped_dialog, b1, b2, b3);
  g_pTable->DoInlineHook(g_hook2193, true);

  // Tear down
  delete wterm;
  delete wrapped_dialog;

  g_ClosedCaption.OnFuncTalk(arg0, arg1);
  printf("<< hooked_f2193\n");
  return ret;
}

// ord=2194
void* g_hook2194 = NULL;
typedef int(*FuncTalkStop)();
FuncTalkStop g_f2194_orig;
void hooked_f2194() {
  printf("[FuncTalkStop]\n");
  g_ClosedCaption.Clear();

  g_pTable->DoInlineHook(g_hook2194, false);
  g_f2194_orig();
  g_pTable->DoInlineHook(g_hook2194, true);
}

// ord=2180
void* g_hook2180 = NULL;
typedef int(*FuncBubbleTalkEnd)(int);
FuncBubbleTalkEnd g_f2180_orig;
void hooked_f2180(int arg0) {
  printf("[FuncBubbleTalkEnd]\n");
  g_ClosedCaption.Clear();

  g_pTable->DoInlineHook(g_hook2180, false);
  g_f2180_orig(arg0);
  g_pTable->DoInlineHook(g_hook2180, true);
}

// ord=2209
// protected: int __thiscall Collection::StoryBoardLibraryComponent::getStoryValue(char const *)
void* g_hook2209 = NULL;
int __fastcall hooked_f2209(void* This, void* unused, char const* x) {

  g_pTable->DoInlineHook(g_hook2209, false);
  int ret = g_f2209_orig(This, x);
  g_TheCollection = This;
  g_pTable->DoInlineHook(g_hook2209, true);

  return ret;
}

struct NiPropertyData {
  int* data;
};

struct NiMessageData {
  int* data;
};

LRESULT CALLBACK WndProcWrapped(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

  g_wndproc_orig(hWnd, message, wParam, lParam);

  switch (message) {
  case WM_KEYDOWN:
//    OnKeyDown(wParam, lParam);
    printf("[dll1] KeyDown\n");
    return 0;
  default: break;
  }
  return 0;
}

BOOL CALLBACK OnEnumWindow(HWND hwnd, LPARAM lparam) {
  wchar_t tmp[100];
  GetWindowTextW(hwnd, tmp, 100);

  wchar_t classname[100];
  GetClassNameW(hwnd, classname, 100);

  bool found = false;
  if ((wcscmp(tmp, L"DX9 Testbed") == 0) || (!wcscmp(classname, L"DX9 Testbed"))) {
    g_hwnd = hwnd;
    g_wndproc_orig = (WndProcType)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG)WndProcWrapped);
    printf("Found game window!\n");
    return FALSE;
  }
  else if ((wcscmp(tmp, L"古剑奇谭") == 0) || (!wcscmp(classname, L"AurogonWindowClass"))) {
    g_hwnd = hwnd;
    //g_wndproc_orig = (WndProcType)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    //SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG)WndProcWrapped);
    printf("Found game window!\n");
    return FALSE;
  }
  else return TRUE;
}



// ord=316
// private: void __thiscall Sequence::DialogueComponent::OnShowVoice(struct NiPropertyData const &)
//
// This function is called when you press Alt+E in game.
// In the original game, a text "\Data\Sounds\xxxxxx.ogg(wav)" will be shown on screen, where xxxxxx
// is the ID of the sound clip.
//

void* g_hook316 = NULL;
typedef void(__thiscall *OnShowVoice)(void* This, struct NiPropertyData const &);
OnShowVoice g_f316_orig;
void __fastcall hooked_f316(void* This, void* unused, struct NiPropertyData const &x) {

  printf("OnShowVoice ");
  int* blah = (int*)(&x);
  for (int i = 0; i < 20; i++) {
    printf("%d ", blah[i]);
  }
  printf("\n");

  g_pTable->DoInlineHook(g_hook316, false);
  g_f316_orig(This, x);
  g_pTable->DoInlineHook(g_hook316, true);
}

// ord=152
// public: virtual void __thiscall Sequence::DialogueComponent::Activate(void)
void* g_hook152 = NULL;
typedef void(__thiscall *DialogueComponentActivate)(void* This);
DialogueComponentActivate g_f152_orig;
void __fastcall hooked_f152(void* This, void* unused) {

  //printf("DialogueComponentActivate() called. 100 int's from self: ");
  //int* x = (int*)This;
  //for (int i = -100; i < 100; i++) {
  //  printf("%d|%08X ", x[i], x[i]);
  //  if (i % 8 == 7) printf("\n");
  //}
  //printf("\n");
  
  // Offending Function ??
  //g_pTable->DoInlineHook(g_hook554, true); // 延迟一点

  g_pTable->DoInlineHook(g_hook152, false);
  g_f152_orig(This);
  g_pTable->DoInlineHook(g_hook152, true);

}

//========================
void* g_hook_swapchain_present = NULL;
typedef void(__thiscall *Swapchain9Present)(void* This, const RECT* pSourceRect, 
                                  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion);
Swapchain9Present g_swapchain_present_orig;
void __fastcall hooked_swapchain_present(void* This, void* unused, const RECT* pSourceRect,
  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion) {
  printf("Swapchain9::Present called\n");
  g_pTable->DoInlineHook(g_hook_swapchain_present, false);
  g_swapchain_present_orig(This, pSourceRect, hDestWindowOverride, pDirtyRegion);
  g_pTable->DoInlineHook(g_hook_swapchain_present, true);
}

// Function to call when seeing the Device for the first time.
// Will set the pointer to the Swapchain
void OnFirstTimeSeeingDev(void* dev) {
  if (g_pSwapchain == NULL) {
    IDirect3DDevice9* d3ddev9 = (IDirect3DDevice9*)dev;
    //g_pSwapchain = 
    HRESULT hr = d3ddev9->GetSwapChain(0, &g_pSwapchain);
    if (SUCCEEDED(hr)) {
      IDirect3DSurface9* backbuffer;
      HRESULT hr = g_pSwapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
      D3DSURFACE_DESC desc;
      backbuffer->GetDesc(&desc);
      // Example: 1600 x 900, format=22 (D3DFMT_X8R8G8B8)
      printf("Obtained swapchain\n");
      printf("Backbuffer desc: %u x %u, format=%d\n", desc.Width, desc.Height, int(desc.Format));
      DetermineUIScaleFactor(desc.Width, desc.Height);
      // Create OpenCV Mat
      //g_surface_mat = new cv::Mat(desc.Height, desc.Width, CV_8UC3);
      //printf("g_surface_mat=%p\n", g_surface_mat);
    }
  }

}

void OnPresent(IDirect3DDevice9* dev) {
  OnFirstTimeSeeingDev(dev);
  Update();

  g_frame_count++;

  int wake_mode = 0;

  if (g_detector_sleeping == true && g_pSurfaceBuf == nullptr && g_pSurfaceCopy == nullptr) {
    if ((g_frame_count % 30) == 29) {
      wake_mode = 1;
    }
    else if ((g_frame_count % 6) == 5) {
      //wake_mode = 2;
    }
  }

  if (wake_mode != 0) {
    // Dump
    IDirect3DSurface9 *bb = NULL;
    HRESULT hr = g_pSwapchain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    D3DSURFACE_DESC d;

    if (/*g_pSurfaceCopy == NULL && */bb != NULL) {
      bb->GetDesc(&d);
      if (SUCCEEDED(hr = g_pD3DDevice->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &g_pSurfaceCopy, NULL))) {

      }
      else {
        printf("[OnPresent] Error: Could not create surface for dump\n");
      }
    }

    // Consumed by detector thd
    if (g_pSurfaceCopy != nullptr && (g_pSurfaceBuf == nullptr)) {
      if (SUCCEEDED(hr = g_pD3DDevice->GetRenderTargetData(bb, g_pSurfaceCopy))) {
        if (SUCCEEDED(hr = D3DXSaveSurfaceToFileInMemory(&g_pSurfaceBuf, D3DXIFF_BMP, g_pSurfaceCopy, NULL, NULL))) {
          int len = int(g_pSurfaceBuf->GetBufferSize());
          printf("[OnPresent] Buffer size: %d, wake mode=%d\n", len, wake_mode);
          unsigned char* chr = (unsigned char*)g_pSurfaceBuf->GetBufferPointer();
          WakeDetectorThread(chr, len, wake_mode);
        }

        //if (g_screenshot_flag) {
        //  std::wstring dest = L"C:\\temp\\frame" + std::to_wstring(g_frame_count) + L".png";
        //  if (SUCCEEDED(hr = D3DXSaveSurfaceToFileW(dest.c_str(), D3DXIFF_PNG, g_pSurfaceCopy, NULL, NULL))) {
        //    wprintf(L"Dumped to [%ls]\n", dest.c_str());
        //  }
        //}
      }
    }
  }

  // Clean unused data to avoid memory leak
  // It looks doing this in the worker thread will hang the program so I'm doing it in the main thread
  if (g_detector_sleeping == true && (g_pSurfaceBuf != nullptr)) {
    g_pSurfaceBuf->Release();
    g_pSurfaceBuf = nullptr;
    g_pSurfaceCopy->Release();
    g_pSurfaceCopy = nullptr;
  }

  //if (g_frame_count == 233)
  //  g_ClosedCaption.LoadDummy();

  g_ClosedCaption.Draw();
  DrawHighlightRects();
}

// This one is __stdcall , and is not like the others.
void* g_hook_d3d9_present = NULL;
typedef void(__stdcall *D3D9Present)(void* This, const RECT* pSourceRect, const RECT* pDestRect,
  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion);
D3D9Present g_d3d9_present_orig;
void __stdcall hooked_d3d9_present(void* This, const RECT* pSourceRect, const RECT* pDestRect,
  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion) {

  OnFirstTimeSeeingDev(This);
  OnPresent(g_pD3DDevice);

  g_pTable->DoInlineHook(g_hook_d3d9_present, false);
  g_d3d9_present_orig(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
  g_pTable->DoInlineHook(g_hook_d3d9_present, true);
}

void InitD3DStuff() {
  // Initialize Overlay Stuff
  HRESULT hr = D3DXCreateFontA(g_pD3DDevice, FONT_SIZE, 0, 0, 0, false, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS,
    CLEARTYPE_QUALITY, DEFAULT_PITCH, "KaiTi", &g_font);

  MyMessageBox::StaticInit();
  g_hover_messagebox = new MyMessageBox();

  if (FAILED(hr)) {
    printf("[Dll1] Failed to create font for overlay\n");
  }
  else {
    printf("[Dll1] Create font okay\n");
  }


  // Create shared Vertex Buffer
  hr = g_pD3DDevice->CreateVertexBuffer(
    8 * sizeof(CustomVertex), 0,
    (D3DFVF_XYZRHW | D3DFVF_DIFFUSE),
    D3DPOOL_DEFAULT,
    &g_vert_buf,
    NULL);
  if (FAILED(hr)) {
    printf("[DLL1] Failed to create Vertex Buffer for MessageBox\n");
  }
}

//========================
// Get D3D9 device
// ord=531 in AurNiDX9Renderer.dll
void* g_hook531 = NULL;
typedef IDirect3DDevice9* (__thiscall *GetD3DDevice)(void* This);
GetD3DDevice g_f531_orig;
IDirect3DDevice9* __fastcall hooked_f531(void* This, void* unused) {
  IDirect3DDevice9* ret;
  g_pTable->DoInlineHook(g_hook531, false);
  ret = g_f531_orig(This);
  if (ret != g_pD3DDevice) {
    g_pD3DDevice = ret;

    InitD3DStuff();
    g_config.Load();
    LoadCaptions();
    LoadImagesAndInitDetectThread();
    EnumWindows(OnEnumWindow, 0); // 找到游戏窗口

    void** VTable1 = *reinterpret_cast<void***>(g_pD3DDevice);
    printf("device vtable[17]   = %p\n", VTable1[17]);

    g_d3d9_present_orig = (D3D9Present)VTable1[17];
    g_hook_d3d9_present = g_pTable->AllocateInlineHookHandle(VTable1[17], hooked_d3d9_present);
    printf("g_hook_d3d9_present = %p\n", g_hook_d3d9_present);
    if (g_hook_d3d9_present) {
      g_pTable->DoInlineHook(g_hook_d3d9_present, true);
    }
  }
  g_pTable->DoInlineHook(g_hook531, true);
  return ret;
}

void* g_hook1259 = NULL;
typedef FILE* (*FOpen)(const char*, const char*);
FOpen g_fopen_orig;
FILE* hooked_fopen(const char* fn, const char* mode) {
  g_pTable->DoInlineHook(g_hook1259, false);
  FILE* ret = g_fopen_orig(fn, mode);
  printf("fopen(%s, %s)\n", fn, mode);
  g_pTable->DoInlineHook(g_hook1259, true);
  return ret;
}

// This one has no export table entry, but has symbol info
// AurProtocol.dll
// public: void __thiscall UIMessage::ShowVoiceMessage::clear_message(void)
// offset: 0x651E0
void* g_hook_ClearVoiceMessage = NULL;
typedef void (__thiscall *ClearVoiceMessage)(void* This);
ClearVoiceMessage g_ClearVoiceMessage_orig;
void __fastcall hooked_ClearVoiceMessage(void* This, void* unused) {
  g_pTable->DoInlineHook(g_hook_ClearVoiceMessage, false);
  printf("UIMessage::ShowVoiceMessage::clear_message(void) called\n");
  g_ClearVoiceMessage_orig(This);
  g_pTable->DoInlineHook(g_hook_ClearVoiceMessage, true);
}

// AurProtocol.dll
// public: void __thiscall UIMessage::ShowVoiceMessage::set_message(
//    class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &)
// offset: 0x65200
void* g_hook_SetVoiceMessage = NULL;
typedef void(__thiscall *SetVoiceMessage)(void* This, const std::string& x);
SetVoiceMessage g_SetVoiceMessage_orig;
void __fastcall hooked_SetVoiceMessage(void* This, void* unused, const std::string& x) {
  g_pTable->DoInlineHook(g_hook_SetVoiceMessage, false);
  printf("UIMessage::ShowVoiceMessage::set_message(%s)\n", x.c_str());
  g_SetVoiceMessage_orig(This, x);
  g_pTable->DoInlineHook(g_hook_SetVoiceMessage, true);
}

// AurProtocol.dll
// public: void __thiscall UIMessage::ShowVoiceMessage::set_message(char const *)
// offset: 0x65290
void* g_hook_SetVoiceMessage1 = NULL;
typedef void(__thiscall *SetVoiceMessage1)(void* This, char const* x);
SetVoiceMessage1 g_SetVoiceMessage1_orig;
void __fastcall hooked_SetVoiceMessage1(void* This, void* unused, char const* x) {
  g_pTable->DoInlineHook(g_hook_SetVoiceMessage1, false);
  printf("UIMessage::ShowVoiceMessage1::set_message(%s)\n", x);
  g_SetVoiceMessage1_orig(This, x);
  g_pTable->DoInlineHook(g_hook_SetVoiceMessage1, true);
}

// AurProtocol.dll
// public:
void* g_hook_ClearDialog = NULL;
typedef void(__thiscall *ClearDialog)(void* This);
ClearDialog g_ClearDialog_orig;
void __fastcall hooked_ClearDialog(void* This) {
  g_pTable->DoInlineHook(g_hook_ClearDialog, false);
  g_ClearDialog_orig(This);
  g_pTable->DoInlineHook(g_hook_ClearDialog, true);
}

typedef char* (__thiscall *NiStringMakeExternalCopy)(void* This);
NiStringMakeExternalCopy g_NiStringMakeExternalCopy_orig;

typedef void(__thiscall *NiStringDtor)(void* This);
NiStringDtor g_NiStringDtor_orig;

typedef char* (__thiscall *NiDataCellAsString)(void* This);
NiDataCellAsString g_NiDataCellAsString_orig;

// AurProtocol.dll
// public: __thiscall UIMessage::ShowVoiceMessage::ShowVoiceMessage(void)

// AurNiEntity.dll
// public: class NiDataCell const & __thiscall NiDataTable::Cell(class NiFixedString const &, int)const
// ord=554
typedef void* (__thiscall *DataTableCell)(void*This, void* str, int);
DataTableCell g_DataTableCell_orig;
void* __fastcall hooked_DataTableCell(void* This, void* unused, void* str, int x) {
  g_pTable->DoInlineHook(g_hook554, false);
  
  void* ret = g_DataTableCell_orig(This, str, x);

  // 印出字串可能导致crash？？
  //printf("NiDataTable::Cell(%s, %d) = %s\n", ch, x, retch);

  char* ch = g_NiStringMakeExternalCopy_orig(str);
  char* retch = g_NiDataCellAsString_orig(ret);
  if (ch && retch)
    if (!strcmp(ch, "AudioFile"))
      g_ClosedCaption.curr_story_idx = atoi(retch);

  g_pTable->DoInlineHook(g_hook554, true);
  return ret;
}

bool last_pressed = false;
bool last_lmb_pressed = false;
void Update() {
  if (g_ClosedCaption.IsVisible()) {
    POINT p, p1;
    GetCursorPos(&p);
    ScreenToClient(g_hwnd, &p);
    if (p.x != g_mouse_x || p.y != g_mouse_y) {
      g_mouse_x = p.x; g_mouse_y = p.y;
      g_ClosedCaption.UpdateMousePos(g_mouse_x, g_mouse_y);
    }
  }

  // Hide?
  bool pressed = false;
  if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) {
    pressed = true;
  }
  // 按键隐显
  if (pressed && (!last_pressed)) {
    g_ClosedCaption.ToggleVisibility();
  }

  // 开始/结束拖拽
  {
    bool lmb_pressed = false;
    if (GetAsyncKeyState(VK_RBUTTON)) {
      lmb_pressed = true;
    }
    if (lmb_pressed == true && last_lmb_pressed == false) {
      if (g_ClosedCaption.IsMouseHover(g_mouse_x, g_mouse_y)) {
        g_ClosedCaption.OnMouseLeftDown(g_mouse_x, g_mouse_y);
      }
    }
    else if (lmb_pressed == false && last_lmb_pressed == true) {
      g_ClosedCaption.OnMouseLeftUp(g_mouse_x, g_mouse_y);
    }
    last_lmb_pressed = lmb_pressed;
  }

  // 拖拽过程
  {
    if (g_ClosedCaption.IsDragging()) g_ClosedCaption.UpdateDrag(g_mouse_x, g_mouse_y);
  }

  // Screenshot
  {
    if ((GetAsyncKeyState(VK_F11) & 0x8000) && (!last_pressed)) {
      g_screenshot_flag = true;
    }
  }

  last_pressed = pressed;
}

extern "C" {
  BOOL CALLBACK Initialize(HMODULE hModuleSelf, PHOOK_FUNCTION_TABLE pTable) {

    ClosedCaption::SetAlignmentPath();

    g_pTable = pTable;
    AllocConsole();

    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w+", stdout);

    SetConsoleOutputCP(CP_UTF8);

    HMODULE h_aurgamecore = GetModuleHandle(L"AurGameCore.dll");
    printf("h_aurgamecore         =%X\n", int(h_aurgamecore));

    HMODULE h_aurfxstudioreference = GetModuleHandle(L"AurFxStudioReference.dll");
    printf("h_aurfxstudioreference=%X\n", int(h_aurfxstudioreference));

    HMODULE h_aurnidx9renderer = GetModuleHandle(L"AurNiDX9Renderer.dll");
    printf("h_aurnidx9renderer    =%X\n", int(h_aurnidx9renderer));

    HMODULE h_aurnientity = GetModuleHandle(L"AurNiEntity.dll");
    printf("h_aurnientity         =%X\n", h_aurnientity);

    HMODULE h_aurnimain = GetModuleHandle(L"AurNiMain.dll");
    printf("h_aurnimain           =%X\n", h_aurnimain);

    HMODULE h_aurprotocol = GetModuleHandle(L"AurProtocol.dll");
    printf("h_aurprotocol         =%X\n", h_aurprotocol);

    {
      LPCSTR x2193 = MAKEINTRESOURCEA(2193);
      g_f2193_orig = (FuncTalk)GetProcAddress(h_aurgamecore, x2193); // Ordinal=2193
      printf("original function #2193 = %p, offset=%llX\n", g_f2193_orig, (long long)g_f2193_orig - (long long)h_aurgamecore);
      g_hook2193 = pTable->AllocateInlineHookHandle(g_f2193_orig, hooked_f2193);
      printf("hook2193 = %p\n", g_hook2193);
      if (g_hook2193) {
        pTable->DoInlineHook(g_hook2193, true);
      }
    }

    {
      LPCSTR x2194 = MAKEINTRESOURCEA(2194);
      g_f2194_orig = (FuncTalkStop)GetProcAddress(h_aurgamecore, x2194);
      printf("original function #2194 = %p, offset=%llX\n", g_f2194_orig, (long long)g_f2194_orig - (long long)h_aurgamecore);
      g_hook2194 = pTable->AllocateInlineHookHandle(g_f2194_orig, hooked_f2194);
      printf("hook2194 = %p\n", g_hook2194);
      if (g_hook2194) {
        pTable->DoInlineHook(g_hook2194, true);
      }
    }


    {
      LPCSTR x2180 = MAKEINTRESOURCEA(2180);
      g_f2180_orig = (FuncBubbleTalkEnd)GetProcAddress(h_aurgamecore, x2180);
      printf("original function #2180 = %p, offset=%llX\n", g_f2180_orig, (long long)g_f2180_orig - (long long)h_aurgamecore);
      g_hook2180 = pTable->AllocateInlineHookHandle(g_f2180_orig, hooked_f2180);
      printf("hook2180 = %p\n", g_hook2180);
      if (g_hook2180) {
        pTable->DoInlineHook(g_hook2180, true);
      }
    }

    {
      LPCSTR x2209 = MAKEINTRESOURCEA(2209);
      g_f2209_orig = (GetStoryValue)GetProcAddress(h_aurgamecore, x2209); // Ordinal=2209
      printf("original function #2209 = %p\n", g_f2209_orig);
      g_hook2209 = pTable->AllocateInlineHookHandle(g_f2209_orig, hooked_f2209);
      printf("hook2209 = %p\n", g_hook2209);
      if (g_hook2209) {
        pTable->DoInlineHook(g_hook2209, true);
      }
    }

    {
      LPCSTR x1065 = MAKEINTRESOURCEA(1065);
      g_ClearDialog_orig = (ClearDialog)GetProcAddress(h_aurprotocol, x1065);
      printf("original function #1065 = %p\n", g_ClearDialog_orig);
      g_hook_ClearDialog = pTable->AllocateInlineHookHandle(g_ClearDialog_orig, hooked_ClearDialog);
      printf("hookCBProtoMessageTalkCtor = %p\n", g_hook_ClearDialog);
      if (g_hook_ClearDialog) {
        pTable->DoInlineHook(g_hook_ClearDialog, true);
      }
    }

    {
      LPCSTR x316 = MAKEINTRESOURCEA(316);
      g_f316_orig = (OnShowVoice)GetProcAddress(h_aurfxstudioreference, x316);
      printf("original function #316 = %p\n", g_f316_orig);
      g_hook316 = pTable->AllocateInlineHookHandle(g_f316_orig, hooked_f316);
      printf("hook316 = %p\n", g_hook316);
      if (g_hook316) {
        pTable->DoInlineHook(g_hook316, true);
      }
    }

    {
      LPCSTR x152 = MAKEINTRESOURCEA(152);
      g_f152_orig = (DialogueComponentActivate)GetProcAddress(h_aurfxstudioreference, x152);
      printf("original function #152 = %p\n", g_f152_orig);
      g_hook152 = pTable->AllocateInlineHookHandle(g_f152_orig, hooked_f152);
      printf("hook152 = %p\n", g_hook152);
      if (g_hook152) {
        pTable->DoInlineHook(g_hook152, true);
      }
    }

    {
      LPCSTR x531 = MAKEINTRESOURCEA(531);
      g_f531_orig = (GetD3DDevice)GetProcAddress(h_aurnidx9renderer, x531);
      printf("original function #531 = %p\n", g_f531_orig);
      g_hook531 = pTable->AllocateInlineHookHandle(g_f531_orig, hooked_f531);
      printf("hook531 = %p\n", g_hook531);
      if (g_hook531) {
        pTable->DoInlineHook(g_hook531, true);
      }
    }

    g_NiStringMakeExternalCopy_orig = (NiStringMakeExternalCopy)GetProcAddress(h_aurnimain, MAKEINTRESOURCEA(5071));
    printf("g_NiStringMakeExternalCopy_orig = %p\n", g_NiStringMakeExternalCopy_orig);

    g_NiStringDtor_orig = (NiStringDtor)GetProcAddress(h_aurnimain, MAKEINTRESOURCEA(588));
    printf("g_NiStringDtor_orig = %p\n", g_NiStringDtor_orig);

    g_NiDataCellAsString_orig = (NiDataCellAsString)GetProcAddress(h_aurnientity, MAKEINTRESOURCEA(521));
    printf("g_NiDataCellAsString_orig = %p\n", g_NiDataCellAsString_orig);

    {
      LPCSTR x554 = MAKEINTRESOURCEA(554);
      g_DataTableCell_orig = (DataTableCell)GetProcAddress(h_aurnientity, x554);
      printf("original function #554 = %p\n", g_DataTableCell_orig);
      g_hook554 = pTable->AllocateInlineHookHandle(g_DataTableCell_orig, hooked_DataTableCell);
      printf("g_hook554 = %p\n", g_hook554);
    }
    EnumWindows(OnEnumWindow, 0);
    return true;
  }

  void SetD3DDevicePtr(void* x) {
    ClosedCaption::SetAlignmentPath();
    g_frame_count = 232; // Fast forward
    printf("[Dll1] device ptr = %p\n", x);
    g_pD3DDevice = (IDirect3DDevice9*)(x);
    InitD3DStuff();
    g_config.Load();
    LoadCaptions();
    LoadImagesAndInitDetectThread();
    // Get HWND
    EnumWindows(OnEnumWindow, 0);
  }

  void SetAudioID(int id) {
    g_ClosedCaption.OnFuncTalk("TEST", "no content");
    g_ClosedCaption.SetAlignmentFileByAudioID(id);
    g_ClosedCaption.FindKeywordsForHighlighting();
  }
}