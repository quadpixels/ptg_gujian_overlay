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
#include <codecvt>
#include <set>
#include <vector>
#include <WinBase.h>

// For global use
IDirect3DDevice9* g_pD3DDevice = NULL;
void* g_hook554 = NULL;

typedef int(__thiscall *GetStoryValue)(void* This, char const* x);
GetStoryValue g_f2209_orig;
void* g_TheCollection = NULL;
int GetStory(char const * x) {
  if (!g_TheCollection) return -999;
  return g_f2209_orig(g_TheCollection, x);
}

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

//========================
// Our overlay stuff ...
std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > g_converter;

const int FONT_SIZE = 18;
LPD3DXFONT g_font;
int g_frame_count = 0;

class ClosedCaption {
public:
  ClosedCaption() { 
    line_width = 360; 
  }
  std::map<std::pair<std::string, std::string>, std::set<int> > index; // (who, content) -> Index
  std::wstring sentence, speaker;
  static std::string alignment_path;
  int line_width;

  static void SetAlignmentPath() {
    char user_directory[233];
    GetEnvironmentVariableA("USERPROFILE", user_directory, 233);
    alignment_path = std::string(user_directory) + std::string("\\Downloads\\GuJian Resources\\dialog_aligned");
  }

  int curr_story_idx;
  int font_size;

  // Index: content -> ID
  void LoadIndex(std::string filename) {
    std::ifstream f(filename.c_str());
    if (f.good()) {
      while (f.good()) {
        int id; std::string content, who;
        f >> id >> who >> content;
        if (who.size() > 0) {
          index[std::make_pair(who, content)].insert(id);
        }
      }
      printf("Loaded index %s, |index|=%lu\n", filename.c_str(), index.size());
      f.close();
    }
  }

  void OnFuncTalk(const char* who, const char* content) {
    printf("[ClosedCaption] OnFuncTalk(%s,%s)\n", who, content);
    int len = MultiByteToWideChar(CP_UTF8, 0, content, -1, NULL, NULL);
    int len_who = MultiByteToWideChar(CP_UTF8, 0, who, -1, NULL, NULL);

    wchar_t* wcontent = new wchar_t[len]; // Hopefully this will be enough!
    wchar_t* wwho = new wchar_t[len_who];
    MultiByteToWideChar(CP_UTF8, 0, content, -1, wcontent, len);
    MultiByteToWideChar(CP_UTF8, 0, who, -1, wwho, len_who);
    
    std::wstring w = wcontent, w1 = wwho;
    speaker = w1;

    delete wcontent;
    delete wwho;

    int ret = FindAlignmentFile(who, content);
    if (ret == -999 || ret == -998) {
      millis2word.clear();
      millis2word[std::make_pair(0.0f, 0.0f)] = w;
      printf("[ClosedCaption] alignment file not found or is not okay. length=%lu\n", w.size());
    }
    else {
      printf("[ClosedCaption] alignment file found\n");
    }
  }

  // Returns status code
  int SetAlignmentFileByAudioID(int id) {
    int ret = -999;
    curr_story_idx = id;
    millis2word.clear();

    char path[263]; // NTFS limit: 262 chars
    sprintf_s(path, "%s\\%d_aligned_label.txt", alignment_path.c_str(), id);
    std::ifstream thefile(path);
    if (thefile.good()) {
      ret = true;
      while (thefile.good()) {
        float t0, t1; std::string word; // UTF-8
        thefile >> t0 >> t1 >> word;
        if (word.size() > 0) { // may be empty at the end of file
          millis2word[std::make_pair(t0, t1)] = g_converter.from_bytes(word); // UTF-16
        }
      }
      start_millis = MillisecondsNow();
    }
    else {
      printf("input file not good\n");
      ret = -998;
    }
    return ret;
  }

  std::wstring GetCurrLine() {
    const long long curr_millis = MillisecondsNow();
    float elapsed = (curr_millis - start_millis) / 1000.0f;
    std::wstring ret = speaker + L"：";
    for (std::map<std::pair<float, float>, std::wstring>::iterator itr =
      millis2word.begin(); itr != millis2word.end(); itr++) {
      if (itr->first.first < elapsed)
        ret += itr->second;
      else break;
    }
    return ret;
  }

  std::wstring GetFullLine() {
    std::wstring ret = speaker + L"：";
    for (std::map<std::pair<float, float>, std::wstring>::iterator itr =
      millis2word.begin(); itr != millis2word.end(); itr++) {
      ret += itr->second;
    }
    return ret;
  }

  // Line: "aaaaaaaaaaaaaaaaaaaaaa"
  //
  // Visual Line: "aaaaaaaaaaa
  //               aaaaaa     "
  //                         ^
  //                         |------ line width limit in px
  //
  std::vector<std::wstring> GetVisualLines(bool is_full) {
    std::vector<std::wstring> ret;
    std::wstring x = is_full ? GetFullLine() : GetCurrLine();
    std::wstring currline;

    float last_w = 0.0f;

    for (int i = 0; i<int (x.size()); i++) {
      RECT rect;
      g_font->DrawTextW(NULL, currline.c_str(), int(currline.size()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
      const float w = rect.right - rect.left;
      
      if ((last_w <= line_width && w > line_width)) {
        ret.push_back(currline);
        currline.clear();
        last_w = 0;
      }
      currline.push_back(x[i]);

      last_w = w;
    }
    if (currline.empty() == false) {
      ret.push_back(currline);
    }

    return ret;
  }

  void Draw() {

    RECT rect;

    RECT rect_header;

    std::vector<std::wstring> s = GetVisualLines(true);
    std::vector<std::wstring> c = GetVisualLines(false);
    for (int i = 0; i<int(s.size()); i++) {

      rect.left = 2;
      rect.top = 32 + i * (1 + FONT_SIZE);

      wchar_t msg[1024];

      // Measure header width
      if (i == 0) wsprintf(msg, L"[ptg]>");
      else wsprintf(msg, L"     >");
      g_font->DrawTextW(NULL, msg, wcslen(msg), &rect_header, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

      // Draw full text
      if (i == 0) wsprintf(msg, L"[ptg]>%ls", s[i].c_str());
      else wsprintf(msg, L"     >%ls", s[i].c_str());
      g_font->DrawTextW(0, msg, wcslen(msg), &rect, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 255, 0));

      // Draw current text
      if (i < c.size()) {
        wsprintf(msg, L"%ls", c[i].c_str());
        rect.left += (rect_header.right - rect_header.left);
        g_font->DrawTextW(0, msg, wcslen(msg), &rect, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 150, 33));
      }
    }
  }
  std::map<std::pair<float, float>, std::wstring> millis2word;

  void LoadDummy() {
    wchar_t s[] = L"蓬莱翻译组当真是十分美妙";
    millis2word.clear();
    for (int i = 0; i < int(wcslen(s)); i++) {
      const float t0 = i * 0.3f, t1 = t0 + 0.3f;
      std::wstring this_one;
      this_one += s[i];
      millis2word[std::make_pair(t0, t1)] = this_one;
    }
    start_millis = MillisecondsNow();
  }

private:
  int last_mainstory;
  long long start_millis;

  int FindAlignmentFile(const char* who, const char* content) { // return TRUE if alignment file is found

    int ret = -999;
    // 第一回尝试：匹配内容
    {
      std::pair<std::string, std::string> key = { who, content };
      if (index.find(key) != index.end()) {
        std::set<int>& ids = index[key];
        if (ids.size() == 1) {
          ret = *(ids.begin());
        }
        else {
          if (ids.find(curr_story_idx + 1) != ids.end())
            ret = (curr_story_idx)+1;
        }
      }
    }

    if (ret != -999) {
      ret = SetAlignmentFileByAudioID(ret);
    }
    printf("[FindAlignmentFile] ret=%d, curr_story_idx=%d\n", ret, curr_story_idx);
    return ret;
  }
};
std::string ClosedCaption::alignment_path = "";

ClosedCaption g_ClosedCaption;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
  printf("[dll1] DllMain called\n");
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

PHOOK_FUNCTION_TABLE g_pTable;

void* g_hook2193 = NULL;
typedef int(*FuncTalk)(char const*, char const*, bool, bool, bool);
FuncTalk g_f2193_orig;
int hooked_f2193(char const* arg0, char const* arg1, bool b1, bool b2, bool b3) {
  g_pTable->DoInlineHook(g_hook2193, false);
  int ret = g_f2193_orig(arg0, arg1, b1, b2, b3);
  g_pTable->DoInlineHook(g_hook2193, true);

  g_ClosedCaption.OnFuncTalk(arg0, arg1);
  return ret;
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

void OnPresent() {
  Update();

  g_frame_count++;
  if (g_frame_count == 233)
    g_ClosedCaption.LoadDummy();
  g_ClosedCaption.Draw();
}

// This one is __stdcall , and is not like the others.
void* g_hook_d3d9_present = NULL;
typedef void(__stdcall *D3D9Present)(void* This, const RECT* pSourceRect, const RECT* pDestRect,
  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion);
D3D9Present g_d3d9_present_orig;
void __stdcall hooked_d3d9_present(void* This, const RECT* pSourceRect, const RECT* pDestRect,
  HWND  hDestWindowOverride, const RGNDATA *pDirtyRegion) {

  OnPresent();

  g_pTable->DoInlineHook(g_hook_d3d9_present, false);
  g_d3d9_present_orig(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
  g_pTable->DoInlineHook(g_hook_d3d9_present, true);
}

void InitD3DStuff() {
  // Initialize Overlay Stuff
  HRESULT hr = D3DXCreateFontA(g_pD3DDevice, FONT_SIZE, 0, 0, 0, false, DEFAULT_CHARSET, OUT_CHARACTER_PRECIS,
    CLEARTYPE_QUALITY, DEFAULT_PITCH, "KaiTi", &g_font);
  if (FAILED(hr)) {
    printf("[Dll1] Failed to create font for overlay\n");
  }
  else {
    printf("[Dll1] Create font okay\n");
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

void LoadCaptions() {

  std::string p = ClosedCaption::alignment_path;

  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\Q01.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\Q03.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\Q07.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\Q10.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\Q11.txt");

  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\M13.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\M14.txt");
  g_ClosedCaption.LoadIndex(p + "\\inverted_index\\M15.txt");
}

void Update() {
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(g_hwnd, &p);
  if (p.x != g_mouse_x || p.y != g_mouse_y) {
    g_mouse_x = p.x; g_mouse_y = p.y;
    printf("Cursor screen pos: %d,%d\n", g_mouse_x, g_mouse_y);
  }
}

BOOL CALLBACK OnEnumWindow(HWND hwnd, LPARAM lparam) {
  wchar_t tmp[100];
  GetWindowTextW(hwnd, tmp, 100);
  bool found = false;
  if (wcscmp(tmp, L"DX9 Testbed") == 0) {
    g_hwnd = hwnd;
    return FALSE;
  }
  else if (wcscmp(tmp, L"古剑奇谭") == 0) {
    g_hwnd = hwnd;
    return FALSE;
  }
  else return TRUE;
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

    LoadCaptions();
    return true;
  }

  void SetD3DDevicePtr(void* x) {
    ClosedCaption::SetAlignmentPath();
    g_frame_count = 232; // Fast forward
    printf("[Dll1] device ptr = %p\n", x);
    g_pD3DDevice = (IDirect3DDevice9*)(x);
    InitD3DStuff();
    LoadCaptions();
    // Get HWND
    EnumWindows(OnEnumWindow, 0);
  }

  void SetAudioID(int id) {
    g_ClosedCaption.SetAlignmentFileByAudioID(id);
  }
}