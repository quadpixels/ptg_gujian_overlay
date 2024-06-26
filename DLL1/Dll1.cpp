﻿// dllmain.cpp : Defines the entry point for the DLL application.

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
#include <assert.h>
#include "config.h"
#include <Psapi.h> // GetModuleFileNameEx

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

extern "C" {
#include "checksum.h"
}

// Resources are included at compile time
// So this means during compiling the resource must exist on disk!
#include "resource1.h"

mz_zip_archive g_archive_inverted_index;
mz_zip_archive g_archive_dialog_aligned;
mz_zip_archive g_archive_parallel_text;
mz_zip_archive g_archive_match_templates;
mz_zip_archive g_archive_miscellaneous;
void DetermineUIScaleFactor(const int w, const int h);

// For global use
IDirect3DDevice9* g_pD3DDevice = NULL;
IDirect3DTexture9* g_pTexture = NULL;
void* g_hook554 = NULL;
IDirect3DSwapChain9* g_pSwapchain = NULL;
IDirect3DSurface9* g_pSurfaceCopy = NULL; // For copy
LPD3DXBUFFER g_pSurfaceBuf = NULL;

extern IDirect3DVertexBuffer9* g_vert_buf;
extern IDirect3DStateBlock9* g_pStateBlock;
bool g_f11_flag;
bool g_use_detect = false;
CRITICAL_SECTION g_critsect_rectlist, g_calc_dimension;
extern volatile bool g_detector_sleeping;
long long g_last_detect_millis = 0;
extern float g_ui_scale_factor;

extern void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor);
#ifndef USE_DETECT_PATH_2
extern void WakeDetectorThread(unsigned char* ch, const int len, const int mode);
#else
extern void WakeDetectorThread(D3DLOCKED_RECT* rect, const int mode);
#endif
extern void DrawHighlightRects();
extern void DetermineUIScaleFactor(const int w, const int h);
extern void LoadImagesFromResource();
extern void ClearHighlightRects();
extern std::string ToMBString(std::wstring s);
extern std::wstring ToWstring(std::string s);

void ShowVideoSubtitles(const std::string&);
void StopCurrentVideoSubtitle();

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
int g_win_w, g_win_h;

PtgOverlayConfig g_config;

//========================
// Our overlay stuff ...
std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > g_converter;

int FONT_SIZE = 26;
LPD3DXFONT g_font = nullptr;
LPD3DXFONT g_font_talk_mock = nullptr; // For mocking the talk font (visually appears to be between 23 and 24)
int g_frame_count = 0;

#include "ClosedCaption.h"

MyMessageBox* g_hover_messagebox, *g_dbg_messagebox;

ClosedCaption g_ClosedCaption;
VideoSubtitles g_VideoSubtitles_intro, g_VideoSubtitles_credits;
VideoSubtitles* g_curr_VideoSubtitle;

void do_LoadCaptions() {

  // TODO: Put parallel texts into an archive file
#if 0
  g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\SeqDlgM01_Rev.txt").c_str());
  g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\DlgM01_Rev.txt").c_str());
  g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\SeqDlgQ01_Rev.txt").c_str());
  g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\DlgQ01_Rev.txt").c_str());
  g_ClosedCaption.LoadEngChsParallelText((g_config.root_path + "\\SeqDlgM02_Rev.txt").c_str());
  g_ClosedCaption.LoadProperNamesList((g_config.root_path + "\\ProperNames.txt").c_str());
#endif

  // TEST
  //g_ClosedCaption.LoadA3AlignmentData("C:\\Temp\\tran_align\\c2e.final");
#ifdef _DEBUG
  g_ClosedCaption.LoadEngChsParallelText(
    "C:\\Users\\nitroglycerine\\Downloads\\Gujian Resources\\parallel_text\\SeqDlgM01_Rev.txt",
    "C:\\Temp\\tran_align\\seqdlgm01_e2c.txt",
    "C:\\Temp\\tran_align\\seqdlgm01_c2e.txt");
#endif

  HMODULE hm = GetModuleHandleA("ptg_overlay.dll"); // Not zero, b/c that may be the game exe
  {
    //
    // Resources list:
    //   #1: aligned dialogs
    //   #2: inverted index
    //   #3: parallel text
    //   #4: match templates
    //   #5: miscellaneous
    //
    LPWSTR resource_ids[] = {
      MAKEINTRESOURCE(IDR_RCDATA1),
      MAKEINTRESOURCE(IDR_RCDATA2),
      MAKEINTRESOURCE(IDR_RCDATA3),
      MAKEINTRESOURCE(IDR_RCDATA4),
      MAKEINTRESOURCE(IDR_RCDATA5),
    };
    mz_zip_archive* archives[] = {
      &g_archive_dialog_aligned,
      &g_archive_inverted_index,
      &g_archive_parallel_text,
      &g_archive_match_templates,
      &g_archive_miscellaneous,
    };

    for (int i = 0; i < sizeof(resource_ids) / sizeof(resource_ids[0]); i++) {
      HRSRC hRes = FindResource(hm, resource_ids[i], RT_RCDATA); // dialog_aligned, inverted_index
      if (hRes != 0) {
        HGLOBAL hData = LoadResource(hm, hRes);
        if (hData != 0) {
          DWORD data_size = SizeofResource(hm, hRes);
          void* ptr = LockResource(hData);

          mz_zip_archive* archive = archives[i];
          void* ptr_copy = malloc(data_size);
          memcpy(ptr_copy, ptr, data_size);

          memset(archive, 0x00, sizeof(*archive));
          archive->m_pRead = mz_zip_mem_read_func;
          mz_bool ok = mz_zip_reader_init_mem(archive, ptr_copy, data_size, 0); // should be ok!
          printf("[LoadCaptions] Loaded Resource #%d, result=%d\n", i, int(ok));

          UnlockResource(hData);
        }
        else printf("[do_LoadCaptions] hData == 0\n");
      }
      else printf("[do_LoadCaptions] hRes == 0\n");
    }
  }

  // Aligned dialog will be loaded as needed

  // TODO: Clean up the following reading function calls !

  // Inverted Index
  {
    char fn[266];
    size_t sz;
    // Extract from tgz file

    for (int q = 1; q <= 17; q++) {
      sprintf_s(fn, "Q%02d.txt", q);
      void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_inverted_index, fn, &sz, 0);
      //printf("%s = %u bytes\n", fn, unsigned(sz));
      g_ClosedCaption.LoadSeqDlgIndexFromMemory(buf, int(sz));
      delete[] buf;
    }
    for (int m = 1; m <= 19; m++) {
      sprintf_s(fn, "M%02d.txt", m);
      void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_inverted_index, fn, &sz, 0);
      //printf("%s = %u bytes\n", fn, unsigned(sz));
      g_ClosedCaption.LoadSeqDlgIndexFromMemory(buf, int(sz));
      delete[] buf;
    }
  }

  // Parallel text
  {
    char fn[256];
    size_t sz;
    for (int q = 1; q <= 17; q++) {
      // Dlg ..
      const char* dlgs[] = { "DlgQ%02d_Rev.txt", "DlgQ%02d.txt" };
      for (int i = 0; i < 2; i++) {
        sprintf_s(fn, dlgs[i], q);
        void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_parallel_text, fn, &sz, 0);
        if (buf != nullptr && sz > 0) {
          printf("Starting to read parallel text [%s] ...\n", fn);
          g_ClosedCaption.LoadEngChsParallelTextFromMemory((const char*)buf, sz);
          delete[] buf;
          buf = nullptr;
        }
      }
      // SeqDlg ..
      const char* seqdlgs[] = { "SeqDlgQ%02d_Rev.txt", "SeqDlgQ%02d.txt" };
      for (int i = 0; i < 2; i++) {
        sprintf_s(fn, seqdlgs[i], q);
        void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_parallel_text, fn, &sz, 0);
        if (buf != nullptr && sz > 0) {
          printf("Starting to read parallel text [%s] ...\n", fn);
          g_ClosedCaption.LoadEngChsParallelTextFromMemory((const char*)buf, sz);
          delete[] buf;
          buf = nullptr;
        }
      }
    }

    for (int m = 1; m <= 19; m++) {
      // Dlg ..
      const char* dlgs[] = { "DlgM%02d_Rev.txt", "DlgM%02d.txt" };
      for (int i = 0; i < 2; i++) {
        sprintf_s(fn, dlgs[i], m);
        void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_parallel_text, fn, &sz, 0);
        if (buf != nullptr && sz > 0) {
          printf("Starting to read parallel text [%s] ...\n", fn);
          g_ClosedCaption.LoadEngChsParallelTextFromMemory((const char*)buf, sz);
          delete[] buf;
          buf = nullptr;
        }
      }
      // SeqDlg ..
      const char* seqdlgs[] = { "SeqDlgM%02d_Rev.txt", "SeqDlgM%02d.txt" };
      for (int i = 0; i < 2; i++) {
        sprintf_s(fn, seqdlgs[i], m);
        void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_parallel_text, fn, &sz, 0);
        if (buf != nullptr && sz > 0) {
          printf("Starting to read parallel text [%s] ...\n", fn);
          g_ClosedCaption.LoadEngChsParallelTextFromMemory((const char*)buf, sz);
          delete[] buf;
          buf = nullptr;
        }
      }
    }

    { // System Dialogs
      sprintf_s(fn, "Sysdlg.txt");
      void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_parallel_text, fn, &sz, 0);
      if (buf != nullptr && sz > 0) {
        printf("Starting to read parallel text [%s] ...\n", fn);
        g_ClosedCaption.LoadEngChsParallelTextFromMemory((const char*)buf, sz);
        delete[] buf;
        buf = nullptr;
      }
    }
  }

  // Proper nouns list & intro subtitles
  {
    size_t sz;
    void* buf = mz_zip_reader_extract_file_to_heap(&g_archive_miscellaneous, "ProperNames.txt", &sz, 0);
    if (buf != nullptr && sz > 0) {
      g_ClosedCaption.LoadProperNamesListFromMemory((const char*)buf, sz);
      delete buf;
    }

    buf = mz_zip_reader_extract_file_to_heap(&g_archive_miscellaneous, "vs_intro.txt", &sz, 0);
    if (buf != nullptr && sz > 0) {
      g_VideoSubtitles_intro.LoadFromMemory((const char*)buf, sz);
      delete buf;
    }

    buf = mz_zip_reader_extract_file_to_heap(&g_archive_miscellaneous, "vs_credits.txt", &sz, 0);
    if (buf != nullptr && sz > 0) {
      g_VideoSubtitles_credits.LoadFromMemory((const char*)buf, sz);
      delete buf;
    }
  }

  LoadImagesFromResource();
}

DWORD WINAPI MyLoaderThreadFunction(LPVOID lpParam) {
  printf(">>> Loader Thread");
  g_ClosedCaption.LoadDummy(L"(Loading resources ...)");
  do_LoadCaptions();
  g_ClosedCaption.LoadDummy(nullptr);
  printf("<<< Loader Thread");
  return 0;
}

void LoadCaptions() {
  bool use_thread = true;
  if (!use_thread) {
    do_LoadCaptions();
  }
  else {
    DWORD loader_thd_id;
    HANDLE loader_thd = CreateThread(
      nullptr,
      0,
      MyLoaderThreadFunction,
      nullptr,
      0,
      &loader_thd_id
    );
  }
}

void LoadImagesAndInitWorkerThreads();

BOOL APIENTRY DllMain(HMODULE hModule,
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
typedef LRESULT(CALLBACK *WndProcType)(HWND, UINT, WPARAM, LPARAM);
WndProcType g_wndproc_orig;

PHOOK_FUNCTION_TABLE g_pTable;

typedef int(__thiscall *GetStoryValue)(void* This, char const* x);
GetStoryValue g_f2209_orig;
void* g_TheCollection = NULL;
int GetStory(char const * x) {
  if (!g_TheCollection) return -999;
  return g_f2209_orig(g_TheCollection, x);
}

//
// 调用流程：
// >> FuncTalk
//     >> hook & word-wrap & other pre-processing
//         <> 调用游戏本体的FuncTalk
//         >> ClosedCaption::FuncTalk Eng->Chs映射
//             >> ClosedCaption::FindAlignmentFile
//                <> SetAlignmentFileByAudioID
//             <<
//             <> ComputeBoundingBoxPerChar
//             <> FindKeywordsForHighlighting
//         <<
//     <<
//
// 新功能：需要手动word wrap（使得单词在换行时保持完整）
void* g_hook2193 = NULL;
typedef int(*FuncTalk)(char const*, char const*, bool, bool, bool);
FuncTalk g_f2193_orig;
int hooked_f2193(char const* arg0, char const* arg1, bool b1, bool b2, bool b3) {
  printf(">> hooked_f2193\n");
  // Word Wrap!
  const int LINE_WIDTH = 90; // talk.swf does NOT do word-wrap (while info.swf does). So, we do it here. Doing it here can also help prevent overflow
  const int LINE_WIDTH_PX = 730; // 730 px @ 1.0X UI scale
  bool enable_line_width_limit = true;
  const bool wrap = false;
  std::string wrapped_mb;

  // Convert to wchar_t string for processing
  int len_dialog = MultiByteToWideChar(CP_UTF8, 0, arg1, -1, NULL, NULL);
  printf("   len_dialog=%d\n", len_dialog);
  wchar_t* wterm = new wchar_t[std::max(4, len_dialog)];
  MultiByteToWideChar(CP_UTF8, 0, arg1, -1, wterm, len_dialog);

  std::wstring wrapped;
  std::wstring curr_word = L" ";
  std::wstring curr_line = curr_word; // For layout usage only
  int pos = 0, control_len = 0;
  bool is_in_control = false;
  bool has_non_english_symbols = false;
  const std::wstring eng = L" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*()_+-=[]{}';\":/?.>,<";
  for (int i = 0; i <= len_dialog; i++) {
    if (eng.find(wterm[i]) == std::wstring::npos) {
      has_non_english_symbols = true;  // 可能是中文对白
    }
    if (wterm[i] == L'*') {
      control_len += 2;
      if (!is_in_control) control_len++; // Opening *b --> +2;  Closing * --> +1
      else {
        // Fix for "*B There is a way... feet,*But
        if (i + 1 < len_dialog && wterm[i] != L' ') {
          curr_word.push_back(L' ');
        }
      }
      is_in_control = !is_in_control;
    }

    if (i == len_dialog || (i < len_dialog && (wterm[i] == L' ' || wterm[i] == L'$'))) {

      bool break_line = false;

      // Exceeds line limit (LINE_WIDTH chars)
      if ((int(pos + curr_word.size()) > LINE_WIDTH + control_len) && (enable_line_width_limit)) {
        break_line = true;
      }

      if (wterm[i] == L'$') {
        break_line = true;
      }

      // Exceeds pixel width? (730 px wide at 1X UI)
      {
        std::wstring tmp = curr_line + curr_word;
        RECT rect;
        g_font_talk_mock->DrawTextW(NULL, tmp.c_str(), int(tmp.size()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
        int px_width = int(0.98 * (rect.right - rect.left)) + 15;
        printf("curr_line=[%s] width=%d control_len=%d linewidth=%d pos=%d\n",
          ToMBString(tmp).c_str(), px_width, control_len, int(pos + curr_word.size()), pos);
        if (px_width > LINE_WIDTH_PX) {
          break_line = true;
        }
      }

      if (break_line) {
        for (int p = pos; p < LINE_WIDTH + control_len; p++) {
          wrapped.push_back(L' ');
        }
        printf("pushed %d spaces\n", LINE_WIDTH + control_len - pos + 1);
        pos = 0;
        control_len = 0;
        curr_word = L" " + curr_word; // 回避【大写字母在句首=转义】的问题 Fix "capital letter @ position 0 interpreted as formatting character" problem
        curr_line = L"";
      }

      if (wterm[i] == L' ') {
        pos += curr_word.size();
        wrapped += curr_word;
        curr_line += curr_word;
      }
      else {
        control_len = 1;
        pos = 0;
        wrapped += curr_word;
        curr_line.clear();
      }

      pos += 1;
      wrapped += wterm[i];
      curr_line += wterm[i];

      curr_word = L"";
    }
    else if (i < len_dialog) {
      if (has_non_english_symbols) {
        bool break_line = false;
        std::wstring tmp = curr_line + curr_word + wterm[i];
        RECT rect;
        g_font_talk_mock->DrawTextW(NULL, tmp.c_str(), int(tmp.size()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
        int px_width = int(0.98 * (rect.right - rect.left)) + 15;
        printf("curr_line=[%s] width=%d control_len=%d linewidth=%d pos=%d\n",
          ToMBString(tmp).c_str(), px_width, control_len, int(pos + curr_word.size()), pos);
        if (px_width > LINE_WIDTH_PX) {
          break_line = true;
        }
        if (break_line) {
          wrapped += curr_word;
          pos += curr_word.size();
          for (int p = pos; p < LINE_WIDTH + control_len; p++) {
            wrapped.push_back(L' ');
          }
          printf("pushed %d spaces\n", LINE_WIDTH + control_len - pos + 1);
          pos = 0;
          control_len = 0;
          curr_word = L" "; // 回避【大写字母在句首=转义】的问题 Fix "capital letter @ position 0 interpreted as formatting character" problem
          curr_line = L"";
        }
      }

      curr_word.push_back(wterm[i]);
    }
  }
  printf("\n");
  wrapped_mb = ToMBString(wrapped);
  delete wterm;

  int ret;
  // HOOK
  g_pTable->DoInlineHook(g_hook2193, false);
  {
    // Handle $TeamLeader replacements
    std::string s0(arg0);
    std::map<std::wstring, std::string> team_leaders = {
      { L"韩云溪", "Han Yunxi" }
    };
    const char* speaker = arg0;
    
    std::wstring w0 = ToWstring(s0);
    if (team_leaders.find(w0) != team_leaders.end()) {
      speaker = team_leaders[w0].c_str();
    }

    ret = g_f2193_orig(speaker, wrapped_mb.c_str(), b1, b2, b3);
  }


  g_pTable->DoInlineHook(g_hook2193, true);

  // Tear down

  g_ClosedCaption.OnFuncTalk(arg0, arg1);
  printf("<< hooked_f2193 arg0=%s arg1=%s\n", arg0, arg1);
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
      printf("[OnFirstTimeSeeingDev] Backbuffer desc: %u x %u, format=%d\n", desc.Width, desc.Height, int(desc.Format));
      DetermineUIScaleFactor(desc.Width, desc.Height);
      g_win_w = int(desc.Width); g_win_h = int(desc.Height);
      // Create OpenCV Mat
      //g_surface_mat = new cv::Mat(desc.Height, desc.Width, CV_8UC3);
      //printf("g_surface_mat=%p\n", g_surface_mat);
    }
  }

}

bool my_flag = false;
void OnPresent(IDirect3DDevice9* dev) {
  OnFirstTimeSeeingDev(dev);
  Update();

  g_frame_count++;

  int wake_mode = 0;
  long long millis = MillisecondsNow();

  if (g_use_detect) {
    if (g_detector_sleeping == true && g_pSurfaceBuf == nullptr && g_pSurfaceCopy == nullptr) {
      //if ((g_frame_count % 30) == 29) 
      if (millis - g_last_detect_millis > 500)
      {
        wake_mode = 1;
        g_last_detect_millis = millis;
      }
      else if ((g_frame_count % 6) == 5) {
        //wake_mode = 2;
      }
    }
  }
  else {
    ClearHighlightRects();
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
        /*
        D3DLOCKED_RECT locked_rect;
        hr = g_pSurfaceCopy->LockRect(&locked_rect, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
          if (!my_flag) {
            cv::Mat3b tmp(g_win_h, g_win_w);
            for (int y = 0; y < g_win_h; y++) {
              for (int x = 0; x < g_win_w; x++) {
                unsigned char *p = (unsigned char*)(locked_rect.pBits) + locked_rect.Pitch * y + x * 4;
                cv::Vec3b rgb = { p[0], p[1], p[2] };
                tmp.at<cv::Vec3b>(y, x) = rgb;
              }
            }
            std::vector<unsigned char> encoded_data;
            cv::imencode(".png", tmp, encoded_data);
            FILE* f;
            fopen_s(&f, "C:\\temp\\aaaaaaaaaaaaaaaaa.png", "wb");
            fwrite(encoded_data.data(), 1, encoded_data.size(), f);
            fclose(f);
            my_flag = true;
          }
          g_pSurfaceCopy->UnlockRect();
        }*/

#ifdef USE_DETECT_PATH_2
        D3DLOCKED_RECT rect;
        if (SUCCEEDED(g_pSurfaceCopy->LockRect(&rect, nullptr, 0))) {
          WakeDetectorThread(&rect, wake_mode);
        }
#else
        if (SUCCEEDED(hr = D3DXSaveSurfaceToFileInMemory(&g_pSurfaceBuf, D3DXIFF_BMP, g_pSurfaceCopy, NULL, NULL))) {
          int len = int(g_pSurfaceBuf->GetBufferSize());
          //printf("[OnPresent] Buffer size: %d, wake mode=%d\n", len, wake_mode);
          unsigned char* chr = (unsigned char*)g_pSurfaceBuf->GetBufferPointer();
          WakeDetectorThread(chr, len, wake_mode);
        }
#endif

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
  if (g_detector_sleeping) {
    if (g_pSurfaceBuf != nullptr) {
      g_pSurfaceBuf->Release();
      g_pSurfaceBuf = nullptr;
    }
    if (g_pSurfaceCopy != nullptr) {
      g_pSurfaceCopy->UnlockRect();
      g_pSurfaceCopy->Release();
      g_pSurfaceCopy = nullptr;
    }
  }

  g_ClosedCaption.Update();

  // CC
  if (g_ClosedCaption.IsVisible()) {
    g_ClosedCaption.Draw();
  }

  // VideoSub
  if (g_curr_VideoSubtitle) {
    g_curr_VideoSubtitle->Update();

    g_curr_VideoSubtitle->Draw();
  }

  if (g_use_detect) DrawHighlightRects();
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

void InitFont(LPD3DXFONT* pfont, int size) {
  if ((*pfont) != nullptr) (*pfont)->Release();

  HINSTANCE hInstance = (HINSTANCE)GetModuleHandleA("ptg_overlay.dll");
  HRSRC res = FindResource(hInstance, MAKEINTRESOURCE(IDR_FONT1), RT_FONT);
  HRESULT hr;
  HANDLE font_handle = 0;
  DWORD nFonts;
  HGLOBAL hg;
  size_t len;
  if (hInstance) {
    hg = LoadResource(hInstance, res);
    void* data = LockResource(hg);
    len = SizeofResource(hInstance, res);

    font_handle = AddFontMemResourceEx(data, len, nullptr, &nFonts);
    hr = D3DXCreateFontW(g_pD3DDevice, size, 0, 0, 0, false, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"华文楷体", pfont); // use W so that font loads correct on English locale
    if (FAILED(hr)) {
      assert(0);
    }
  }

  printf("[InitFont size=%d] hInstance=%X, res=%X, font_handle=%X, hr=%d\n",
    size, hInstance, res, font_handle, int(hr));
}

void InitD3DStuff() {
  // Initialize Overlay Stuff
  InitFont(&g_font, int(FONT_SIZE * 1.0f));

  // Roughly 24 px * 628 / 641 (0.98) at 1280x800
  InitFont(&g_font_talk_mock, 24);

  MyMessageBox::StaticInit();
  g_hover_messagebox = new MyMessageBox();
  g_dbg_messagebox = new MyMessageBox();
  g_dbg_messagebox->x = 0;
  g_dbg_messagebox->y = 20;
  g_dbg_messagebox->gravity = 7; // Anchor point is top-left corner
  g_dbg_messagebox->text_align = -1;

  // Create shared Vertex Buffer
  HRESULT hr = g_pD3DDevice->CreateVertexBuffer(
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
    LoadImagesAndInitWorkerThreads();
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
typedef void(__thiscall *ClearVoiceMessage)(void* This);
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

// AurGameCore.dll
// public: static class NiEntityPropertyInterface * __cdecl Game::BinkVideoComponent::Create(char const *)
typedef void*(__cdecl *BinkVideoComponentCreate) (char const*);
BinkVideoComponentCreate g_BinkVideoComponentCreate_orig;
void* g_hook659 = nullptr;
void* __cdecl hooked_BinkVideoComponentCreate(char const* x) {
  g_pTable->DoInlineHook(g_hook659, false);
  printf("[BinkVideoComponent::Create] name=%s\n", x);
  void* ret = g_BinkVideoComponentCreate_orig(x);
  g_pTable->DoInlineHook(g_hook659, true);
  return ret;
}

// Bink video play hook
// AurGameCore.dll
// protected: void __thiscall Game::BinkVideoComponent::OnPlayBink(struct NiMessageData const &)
typedef void(__thiscall *BinkVideoComponentOnPlayBink) (void* This, void* arg1);
BinkVideoComponentOnPlayBink g_BinkVideoComponentOnPlayBink_orig;
void* g_hook1601 = nullptr;
void __fastcall hooked_BinkVideoComponentOnPlayBink(void* This, void* unused, void* arg1) {
  g_pTable->DoInlineHook(g_hook1601, false);
  printf("[BinkVideoComponent::OnPlayBink] This=%p\n", This); // This will be the same for multiple Bink videos

  int* iparg1 = (int*)arg1;
  if (iparg1 != nullptr) {
    if (iparg1[1] == 0xE3F434 && iparg1[7] == 0xE3F434) { // Intro movie
      ShowVideoSubtitles("intro");
    }
  }
  g_BinkVideoComponentOnPlayBink_orig(This, arg1);
  g_pTable->DoInlineHook(g_hook1601, true);
}

// Bink video stop hook
// AurGameCore.dll
// protected: void __thiscall Game::BinkVideoComponent::OnStopBink(struct NiMessageData const &)
typedef void(__thiscall *BinkVideoComponentOnStopBink)(void* This, void* arg1);
BinkVideoComponentOnStopBink g_BinkVideoComponentOnStopBink_orig;
void* g_hook1662 = nullptr;
void __fastcall hooked_BinkVideoComponentOnStopBink(void* This, void* unused, void* arg1) {
  g_pTable->DoInlineHook(g_hook1662, false);

  StopCurrentVideoSubtitle();

  g_BinkVideoComponentOnStopBink_orig(This, arg1);
  g_pTable->DoInlineHook(g_hook1662, true);
}

typedef bool(__thiscall *NiInputMouseGetPositionDelta)(void* This, int* arg1, int* arg2, int* arg3);
NiInputMouseGetPositionDelta g_NiInputMouseGetPositionDelta_orig;
void* g_hook239 = nullptr;
int delta_x_buf = 0;
bool g_mouse_rotation_fix_enabled = true;
bool __fastcall hooked_NiInputMouseGetPositionDelta(void* This, void* unused, int* arg1, int* arg2, int* arg3) {
  g_pTable->DoInlineHook(g_hook239, false);
  bool ret = g_NiInputMouseGetPositionDelta_orig(This, arg1, arg2, arg3);

#ifdef _DEBUG
  printf("[NiInputMouseGetPositionDelta] %d %d %d\n", *arg1, *arg2, *arg3);
#endif

  if ((GetAsyncKeyState('W') & 0x8000) ||
    (GetAsyncKeyState('w') & 0x8000) ||
    (GetAsyncKeyState(VK_UP) & 0x8000)) {
    delta_x_buf += (*arg1);
    const int RATIO = 5;
    if (delta_x_buf > RATIO || delta_x_buf < -RATIO) {
      int sign = (delta_x_buf > RATIO) ? 1 : -1;
      int absval = delta_x_buf * sign;
      *arg1 = (absval / RATIO) * sign;
      delta_x_buf = (absval % RATIO) * sign;
    }
  }
  else {
    delta_x_buf = 0;
  }

  g_pTable->DoInlineHook(g_hook239, true);
  return ret;
}

// ====================================================================================

bool last_pressed = false;
bool last_lmb_pressed = false;
int rctrl_count = 0;
void Update() {

  {
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
    g_ClosedCaption.Hover();
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
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) {
      if (!last_pressed) {
        if (rctrl_count > 7) {
          g_ClosedCaption.ToggleIsDebug();
        }
        else rctrl_count++;
      }
      pressed = true;
    }
  }

  // Detector
  {
    if (GetAsyncKeyState(VK_F11) & 0x8000) {
      if (!last_pressed) {
        g_ClosedCaption.ToggleIsEnabled();
      }
      pressed = true;
    }
  }

  last_pressed = pressed;
}

extern "C" {
  BOOL CALLBACK Initialize(HMODULE hModuleSelf, PHOOK_FUNCTION_TABLE pTable) {

    g_pTable = pTable;
#ifdef _DEBUG
    AllocConsole();
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w+", stdout);
    SetConsoleOutputCP(CP_UTF8);
#endif


    HMODULE h_aurgamecore = GetModuleHandle(L"AurGameCore.dll");
    printf("h_aurgamecore         =%X\n", int(h_aurgamecore));

    HMODULE h_aurfxstudioreference = GetModuleHandle(L"AurFxStudioReference.dll");
    printf("h_aurfxstudioreference=%X\n", int(h_aurfxstudioreference));

    HMODULE h_aurnidx9renderer = GetModuleHandle(L"AurNiDX9Renderer.dll");
    printf("h_aurnidx9renderer    =%X\n", int(h_aurnidx9renderer));

    HMODULE h_aurnientity = GetModuleHandle(L"AurNiEntity.dll");
    printf("h_aurnientity         =%X\n", int(h_aurnientity));

    HMODULE h_aurnimain = GetModuleHandle(L"AurNiMain.dll");
    printf("h_aurnimain           =%X\n", int(h_aurnimain));

    HMODULE h_aurprotocol = GetModuleHandle(L"AurProtocol.dll");
    printf("h_aurprotocol         =%X\n", int(h_aurprotocol));

    HMODULE h_aurinput = GetModuleHandle(L"AurNiInput.dll");
    printf("h_aurinput            =%X\n", int(h_aurinput));

    HMODULE h_gujianexe = GetModuleHandle(L"GuJian.exe");
    printf("h_gujianexe           =%X\n", int(h_gujianexe));
    HANDLE hproc = GetCurrentProcess();

    if (h_gujianexe != 0 && hproc != 0) {
      char fn[263];
      GetModuleFileNameExA(hproc, h_gujianexe, fn, 265);
      printf("GuJian.exe is located in [%s]\n", fn);

      // Open Gujian.exe and compute CRC
      FILE* f = nullptr; fopen_s(&f, fn, "rb");
      uint32_t crc32val = 0xFFFFFFFF;
      if (f) {
        int ch;
        while ((ch = fgetc(f)) != EOF) {
          crc32val = update_crc_32(crc32val, (unsigned char)ch);
        }
        fclose(f);
        printf("GuJian.exe CRC32=%08X\n", crc32val);
        g_mouse_rotation_fix_enabled = (crc32val == 0x543E3B2E); // Steam Version 2017-10-15
      }
    }

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
      LPCSTR x1601 = MAKEINTRESOURCEA(1601);
      g_BinkVideoComponentOnPlayBink_orig = (BinkVideoComponentOnPlayBink)GetProcAddress(h_aurgamecore, x1601);
      printf("original function #1601 = %p\n", g_BinkVideoComponentOnPlayBink_orig);
      g_hook1601 = pTable->AllocateInlineHookHandle(g_BinkVideoComponentOnPlayBink_orig, hooked_BinkVideoComponentOnPlayBink);
      printf("g_hook1601 = %p\n", g_hook1601);
      if (g_hook1601) {
        g_pTable->DoInlineHook(g_hook1601, true);
      }
    }

    {
      LPCSTR x1662 = MAKEINTRESOURCEA(1662);
      g_BinkVideoComponentOnStopBink_orig = (BinkVideoComponentOnStopBink)GetProcAddress(h_aurgamecore, x1662);
      printf("original function #1662 = %p\n", g_BinkVideoComponentOnStopBink_orig);
      g_hook1662 = pTable->AllocateInlineHookHandle(g_BinkVideoComponentOnStopBink_orig, hooked_BinkVideoComponentOnStopBink);
      printf("g_hook1662 = %p\n", g_hook1662);
      if (g_hook1662) {
        g_pTable->DoInlineHook(g_hook1662, true);
      }
    }

    {
      LPCSTR x659 = MAKEINTRESOURCEA(659);
      g_BinkVideoComponentCreate_orig = (BinkVideoComponentCreate)GetProcAddress(h_aurgamecore, x659);
      printf("original function #659 = %p\n", g_BinkVideoComponentCreate_orig);
      g_hook659 = pTable->AllocateInlineHookHandle(g_BinkVideoComponentCreate_orig, hooked_BinkVideoComponentCreate);
      printf("g_hook659 = %p\n", g_hook659);
      if (g_hook659) {
        g_pTable->DoInlineHook(g_hook659, true);
      }
    }

    {
      LPCSTR x554 = MAKEINTRESOURCEA(554);
      g_DataTableCell_orig = (DataTableCell)GetProcAddress(h_aurnientity, x554);
      printf("original function #554 = %p\n", g_DataTableCell_orig);
      g_hook554 = pTable->AllocateInlineHookHandle(g_DataTableCell_orig, hooked_DataTableCell);
      printf("g_hook554 = %p\n", g_hook554);
    }

    {
      LPCSTR x239 = MAKEINTRESOURCEA(239);
      g_NiInputMouseGetPositionDelta_orig = (NiInputMouseGetPositionDelta)GetProcAddress(h_aurinput, x239);
      printf("original function #239 = %p\n", g_NiInputMouseGetPositionDelta_orig);
      g_hook239 = pTable->AllocateInlineHookHandle(g_NiInputMouseGetPositionDelta_orig, hooked_NiInputMouseGetPositionDelta);
      printf("g_hook239 = %p\n", g_hook239);
      if (g_hook239) {
        g_pTable->DoInlineHook(g_hook239, true);
      }
    }

    EnumWindows(OnEnumWindow, 0);
    return true;
  }

  void SetD3DDevicePtr(void* x) {
    g_frame_count = 232; // Fast forward
    printf("[Dll1] device ptr = %p\n", x);
    g_pD3DDevice = (IDirect3DDevice9*)(x);
    InitD3DStuff();
    g_config.Load();
    LoadCaptions();
    LoadImagesAndInitWorkerThreads();
    // Get HWND
    EnumWindows(OnEnumWindow, 0);
  }

  void SetAudioID(int id) {
    if (id == 1) {
      g_ClosedCaption.OnFuncTalk("$TeamLeader", "The scent of the Wind Seeker Powder is diminishing, should not be that way. Let me take another look!");
    }
    else if (id == 100000) {
      g_ClosedCaption.OnFuncTalk("Han Yunxi", "Haha! Chan, Hurry up!");
      g_ClosedCaption.curr_story_idx = 100000;
    }
    else {
      g_ClosedCaption.test_SetAudioIDWithParallelText(id, true);
    }
    //else {
    //  g_ClosedCaption.SetAlignmentFileByAudioID(id);
    //  g_ClosedCaption.OnFuncTalk("TEST", ToMBString(g_ClosedCaption.GetFullLine(true)).c_str());
    //}
    g_ClosedCaption.ComputeBoundingBoxPerChar();
    g_ClosedCaption.FindKeywordsForHighlighting();
    g_ClosedCaption.FindA3Alignment(g_ClosedCaption.GetCurrOrigText());
  }
}

void ShowVideoSubtitles(const std::string& tag) {
  StopCurrentVideoSubtitle();
  if (tag == "intro") {
    g_VideoSubtitles_intro.Start();
    g_curr_VideoSubtitle = &g_VideoSubtitles_intro;
  }
  else if (tag == "credits") {
    g_VideoSubtitles_credits.Start();
    g_curr_VideoSubtitle = &g_VideoSubtitles_credits;
  }
}

void StopCurrentVideoSubtitle() {
  if (g_curr_VideoSubtitle != nullptr) {
    g_curr_VideoSubtitle->Stop();
    g_curr_VideoSubtitle = nullptr;
  }
}