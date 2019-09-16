#include "ClosedCaption.h"

#include <codecvt>
#include <d3dx9.h>
#include <d3d9.h>
#include "miniz.h"
#include <assert.h>
#include <list>

// in dll1.cpp
extern std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > g_converter;
extern long long MillisecondsNow();
extern LPD3DXFONT g_font;
extern int FONT_SIZE;
extern MyMessageBox* g_hover_messagebox, *g_dbg_messagebox;
extern IDirect3DDevice9* g_pD3DDevice;
extern bool IsDialogBoxMaybeGone();
extern CRITICAL_SECTION g_critsect_rectlist, g_calc_dimension;

IDirect3DVertexBuffer9* g_vert_buf;
IDirect3DStateBlock9* g_pStateBlock;
extern mz_zip_archive g_archive_dialog_aligned;
extern RECT g_curr_dialogbox_rect; // Detect.cpp
extern float g_ui_scale_factor;
extern int g_win_w, g_win_h;
extern bool g_dialog_box_gone;

std::wstring g_header1 = L"";

// Highlight dialog contents according to proper names list
HANDLE g_pnl_highlight_thd_handle;
DWORD  g_pnl_highlight_thd_id;

std::list<std::wstring> g_pnl_workqueue; // Q is safe for extreme cases (e.g. when some user issues 10000 changes in a second)
CRITICAL_SECTION g_pnl_workq_critsect, g_pnl_mask_critsect;

extern void ShowVideoSubtitles(const std::string& tag);

// If we don't use this function, non-English text will be garbled on an English Windows 10
//   with "default non-English locale" set to English
std::wstring ToWstring(std::string s) {
  std::wstring ret;
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, NULL);
  wchar_t* w = new wchar_t[len]; // Hopefully this will be enough!
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, len);
  ret = std::wstring(w);
  delete w;
  return ret;
}

DWORD WINAPI PnlHighlightThdStart(LPVOID lpParam) {
  ClosedCaption* cc = (ClosedCaption*)lpParam;
  while (true) {
    while (g_pnl_workqueue.empty()) {
      Sleep(50);
    }

    EnterCriticalSection(&g_pnl_workq_critsect);
    while (g_pnl_workqueue.size() > 1)
      g_pnl_workqueue.pop_front();
    std::wstring the_work = g_pnl_workqueue.back();
    g_pnl_workqueue.clear();
    LeaveCriticalSection(&g_pnl_workq_critsect);

    cc->do_FindKeywordsForHighlighting(the_work);
  }
}

struct MyD3D9RenderState {
  DWORD alpha_blend_enable, blend_op, blend_op_alpha, src_blend, dest_blend, lighting, cull_mode, alpha_func;
  IDirect3DSurface9* pDsv;
};
MyD3D9RenderState g_render_state;
void BackupD3D9RenderState() {
  g_pD3DDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &(g_render_state.alpha_blend_enable));
  g_pD3DDevice->GetRenderState(D3DRS_BLENDOP, &(g_render_state.blend_op));
  g_pD3DDevice->GetRenderState(D3DRS_BLENDOPALPHA, &(g_render_state.blend_op_alpha));
  g_pD3DDevice->GetRenderState(D3DRS_SRCBLEND, &(g_render_state.src_blend));
  g_pD3DDevice->GetRenderState(D3DRS_DESTBLEND, &(g_render_state.dest_blend));
  g_pD3DDevice->GetRenderState(D3DRS_LIGHTING, &(g_render_state.lighting));
  g_pD3DDevice->GetRenderState(D3DRS_CULLMODE, &(g_render_state.cull_mode));
  g_pD3DDevice->GetRenderState(D3DRS_ALPHAFUNC, &(g_render_state.alpha_func));
  g_pD3DDevice->GetDepthStencilSurface(&(g_render_state.pDsv));
}

void RestoreD3D9RenderState() {
  // Revert alpha blending settings
  g_pD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, g_render_state.alpha_blend_enable);
  g_pD3DDevice->SetRenderState(D3DRS_BLENDOP, g_render_state.blend_op);
  g_pD3DDevice->SetRenderState(D3DRS_BLENDOPALPHA, g_render_state.blend_op_alpha);
  g_pD3DDevice->SetRenderState(D3DRS_SRCBLEND, g_render_state.src_blend);
  g_pD3DDevice->SetRenderState(D3DRS_DESTBLEND, g_render_state.dest_blend);
  g_pD3DDevice->SetRenderState(D3DRS_LIGHTING, g_render_state.lighting);
  g_pD3DDevice->SetDepthStencilSurface(g_render_state.pDsv);
  g_pD3DDevice->SetRenderState(D3DRS_CULLMODE, g_render_state.cull_mode);
  g_pD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, g_render_state.alpha_func);
}

// ========================= HELPER ==============================
void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor) {
  void* pVoid;
  const float Z = -1.0f;
  // 画背景
  CustomVertex quad_bk[] =
  {
    { float(x),     float(y),   Z, 1.0f, bkcolor, },
    { float(x + w), float(y),   Z, 1.0f, bkcolor, },
    { float(x),     float(y + h), Z, 1.0f, bkcolor, },
    { float(x + w), float(y + h), Z, 1.0f, bkcolor, },
  };

  g_vert_buf->Lock(0, 0, (void**)(&pVoid), 0);
  memcpy(pVoid, quad_bk, sizeof(quad_bk));
  g_vert_buf->Unlock();

  // https://www.drunkenhyena.com/cgi-bin/view_cpp_article.pl?chapter=2;article=35
  // Save alpha blending settings ...

  g_pD3DDevice->SetRenderState(D3DRS_LIGHTING, false);
  g_pD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
  g_pD3DDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  g_pD3DDevice->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
  g_pD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  g_pD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

  g_pD3DDevice->SetPixelShader(NULL);
  g_pD3DDevice->SetVertexShader(NULL);
  g_pD3DDevice->SetTexture(0, NULL);
  g_pD3DDevice->SetRenderState(D3DRS_CLIPPING, FALSE);
  g_pD3DDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, FALSE);
  g_pD3DDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);


  g_pD3DDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

  g_pD3DDevice->SetDepthStencilSurface(NULL);

  g_pD3DDevice->SetStreamSource(0, g_vert_buf, 0, sizeof(CustomVertex));
  g_pD3DDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

  // 画线
  CustomVertex quad[] =
  {
    { float(x),    float(y),   Z, 1.0f, bordercolor, },
    { float(x + w), float(y),   Z, 1.0f, bordercolor, },

    { float(x + w), float(y),   Z, 1.0f, bordercolor, },
    { float(x + w), float(y + h), Z, 1.0f, bordercolor, },

    { float(x + w), float(y + h), Z, 1.0f, bordercolor, },
    { float(x),   float(y + h), Z, 1.0f, bordercolor, },

    { float(x),   float(y + h), Z, 1.0f, bordercolor, },
    { float(x),   float(y),   Z, 1.0f, bordercolor, },
  };

  g_vert_buf->Lock(0, 0, (void**)(&pVoid), 0);
  memcpy(pVoid, quad, sizeof(quad));
  g_vert_buf->Unlock();

  g_pD3DDevice->SetStreamSource(0, g_vert_buf, 0, sizeof(CustomVertex));
  g_pD3DDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, 0, 8);
}

std::vector<std::string> MySplitString(const std::string line, const char sep) {
  std::vector<std::string> sp;
  std::string x;
  for (int i = 0; i < int(line.size()); i++) {
    if (line[i] == sep) {
      sp.push_back(x); x = "";
    }
    else {
      x.push_back(line[i]);
    }
  }
  if (x.size() > 0) sp.push_back(x);
  return sp;
}

std::string MyGetLineFromMemory(const char* buf, int* idx, size_t buf_len) {
  std::string line;
  while (true) {
    if (*idx >= int(buf_len)) break;
    bool has_new_line = false;
    while ((*idx) < int(buf_len) && buf[(*idx)] == '\n' || buf[(*idx)] == '\r') {
      has_new_line = true;
      (*idx)++;
    }

    if (has_new_line) break;

    char ch = buf[(*idx)];
    if (ch != '\n' && ch != '\r') {
      line.push_back(ch);
      (*idx)++;
    }
  }
  return line;
}

// ===============================================================

int ClosedCaption::PADDING_LEFT = 0;
int ClosedCaption::PADDING_TOP = 3;
int ClosedCaption::MOUSE_Y_DELTA = 24;
int ClosedCaption::FADE_IN_MILLIS = 300;
int ClosedCaption::FADE_OUT_MILLIS = 2000;

long long g_last_dialogbox_present_millis = 0;

ClosedCaption::ClosedCaption() { 
  opacity_end = 1.0f;
  opacity = 1.0f;
  fade_end_millis = MillisecondsNow() + FADE_IN_MILLIS; // Keep this info for 5 seconds
  fade_duration = FADE_IN_MILLIS;

  line_width = 640;
  visible_height = 16;
  x = 2;
  y = 29;

  is_debug = false;
  is_enabled = true;

  LoadDummy(nullptr);
}

int ClosedCaption::GuessNextAudioID(int id) {
  std::set<int>::iterator itr = all_story_ids.upper_bound(id);
  if (itr == all_story_ids.end()) return id + 1;
  else return *itr;
}

std::wstring g_colon = L"";
std::wstring GetFullWidthColon() {
  if (g_colon.size() < 1) {
    int len = MultiByteToWideChar(CP_UTF8, 0, "：", -1, NULL, NULL);
    wchar_t* wcolon = new wchar_t[len]; // Hopefully this will be enough!
    MultiByteToWideChar(CP_UTF8, 0, "：", -1, wcolon, len);
    g_colon = std::wstring(wcolon);
    delete wcolon;
  }
  return g_colon;
}

std::wstring ClosedCaption::GetCurrLine() {
  const long long curr_millis = MillisecondsNow();
  float elapsed = (curr_millis - start_millis) / 1000.0f;
  std::wstring ret = speaker;
  if (ret.size() > 0) ret += GetFullWidthColon();

  for (std::map<std::pair<float, float>, std::wstring>::iterator itr =
    millis2word.begin(); itr != millis2word.end(); itr++) {
    if (itr->first.first < elapsed)
      ret += itr->second;
    else break;
  }
  return ret;
}

std::wstring ClosedCaption::GetFullLine() {
  std::wstring ret = speaker;
  if (ret.size() > 0) ret += GetFullWidthColon();

  for (std::map<std::pair<float, float>, std::wstring>::iterator itr =
    millis2word.begin(); itr != millis2word.end(); itr++) {
    ret += itr->second;
  }
  return ret;
}

void ClosedCaption::ComputeBoundingBoxPerChar() {
  bb2char.clear();
  std::vector<std::wstring> s = GetVisualLines(true);
  const int left = PADDING_LEFT, y0 = PADDING_TOP;

  int offset = 0; // offset in title

  for (int i = 0; i<int(s.size()); i++) {
    RECT rect;
    rect.left = left; rect.top = y0 + i * (1 + int(FONT_SIZE * g_ui_scale_factor));
    wchar_t msg[1024];

    // Header
    msg, g_header1.c_str();
    g_font->DrawTextW(NULL, msg, wcslen(msg), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    int last_x = rect.right;
    wsprintf(msg, L"%ls%ls", g_header1.c_str(), s[i].c_str());

    int HEADER_LEN = int(g_header1.size());
    
    for (unsigned j = HEADER_LEN; j < wcslen(msg); j++) {
      g_font->DrawTextW(0, msg, j + 1, &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
      const int curr_x = rect.right;
      RECT char_rect = { last_x, rect.top, curr_x, rect.bottom };
      bb2char[char_rect] = offset;
      last_x = curr_x;
      offset++;
    }
  }
  printf("BB has %lu entries\n", bb2char.size());
}

// Line: "aaaaaaaaaaaaaaaaaaaaaa"
//
// Visual Line: "aaaaaaaaaaa
//               aaaaaa     "
//                         ^
//                         |------ line width limit in px
//
std::vector<std::wstring> ClosedCaption::GetVisualLines(bool is_full) {
  std::vector<std::wstring> ret;
  std::wstring x = is_full ? GetFullLine() : GetCurrLine();
  std::wstring currline;

  float last_w = 0.0f;

  for (int i = 0; i<int(x.size()); i++) {
    RECT rect;
    g_font->DrawTextW(NULL, currline.c_str(), int(currline.size()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    const float w = float(rect.right - rect.left);

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

void ClosedCaption::do_DrawHighlightedProperNouns(RECT rect_header, RECT rect, const int curr_text_offset, const std::wstring line, D3DCOLOR color) {
  const int lb = 0, ub = 0 + line.size() - 1;
  std::vector<std::wstring> lines = GetVisualLines(true);
  EnterCriticalSection(&g_pnl_mask_critsect);
  for (const std::pair<int, int> x : proper_noun_ranges) {
    RECT rect_header1 = rect_header, rect1 = rect;
    int lb1 = x.first - curr_text_offset, ub1 = x.first + x.second - 1 - curr_text_offset; // 要减去该行所在的offset

    if (!((ub < lb1) || (ub1 < lb))) {
      int lb_vis = max(lb1, lb), ub_vis = min(ub, ub1);
      //printf("Curr Text:    (lb,  ub)=(%d,%d)\n", lb, ub);
      //printf("Curr Segment: (lb1,ub1)=(%d,%d)\n", lb1, ub1);
      std::wstring preceding = line.substr(0, lb_vis - lb);
      std::wstring content = line.substr(lb_vis, ub_vis - lb_vis + 1);
      std::wstring teststr = g_header1 + preceding;
      g_font->DrawTextW(NULL, teststr.c_str(), teststr.size(), &rect_header1, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
      rect1.left += rect_header1.right - rect_header1.left;
      g_font->DrawTextW(NULL, content.c_str(), int(content.size()), &rect1, DT_NOCLIP, color);
    }
  }
  LeaveCriticalSection(&g_pnl_mask_critsect);
}

void ClosedCaption::do_DrawBorder(int x, int y, int w, int h) {
  
}

void ClosedCaption::Update() {

  if (!is_enabled) return;

  const long long millis = MillisecondsNow();
  const bool dialogbox_maybe_gone = IsDialogBoxMaybeGone();

  // Update Caption playback state
  if (caption_state == CAPTION_PLAYING) {
    float elapsed = (millis - start_millis) / 1000.0f;
    if (millis2word.size() > 0 && elapsed > millis2word.rbegin()->first.second) {
      caption_state = CAPTION_PLAY_ENDED;
      dialog_box_gone_when_fadingout = g_dialog_box_gone;
    }
  }
  else if (caption_state == CAPTION_PLAY_ENDED) { // AUTO HIDE!
    float elapsed = (millis - start_millis) / 1000.0f;
    const float AUTO_HIDE_TIMEOUT = 2; // seconds
    if (millis2word.size() > 0 && 
      elapsed > millis2word.rbegin()->first.second + AUTO_HIDE_TIMEOUT &&
      dialogbox_maybe_gone) {
      caption_state = CAPTION_NOT_PLAYING;
      
      // Show credit screen
      if (curr_story_idx == 100247) {
        ShowVideoSubtitles("credits");
      }
      FadeOut();
    }
  }
  else if (caption_state == CAPTION_PLAYING_NO_TIMELINE) {
    if (!dialogbox_maybe_gone) {
      g_last_dialogbox_present_millis = millis;
    }
    else {
      const long AUTO_HIDE_TIMEOUT_MILLIS = 1000; // msec
      if (millis - g_last_dialogbox_present_millis > AUTO_HIDE_TIMEOUT_MILLIS) {
        dialog_box_gone_when_fadingout = false; // Never dim "NO TIMELINE"
        caption_state = CAPTION_NOT_PLAYING;
        FadeOut();
      }
    }
  }

  // Fading in/out when playback is done
  if (caption_state == CAPTION_NOT_PLAYING) {
    if (is_hovered || is_dragging) {
      this->FadeIn();
    }
    else {
      this->FadeOut();
    }
  }

  // Update Opacity
  if (millis < fade_end_millis - fade_duration) {
    opacity = opacity_start;
  }
  else if (millis < fade_end_millis) {
    float completion = 1.0f - (fade_end_millis - millis) * 1.0f / fade_duration;
    if (completion < 0.0f) {
      completion = 0.0f;
    }
    else if (completion > 1.0f) completion = 1.0f;
    opacity = opacity_start * (1.0f - completion) + opacity_end * completion;
  }
  else {
    opacity = opacity_end;
  }
}

void ClosedCaption::Draw() {
  long long millis = MillisecondsNow();

  if (!is_enabled) return;
  if (IsVisible() == false) return;

  // 保存先前的渲染状态
  BackupD3D9RenderState();

  RECT rect = { 0, 0, 0, 0 };
  RECT rect_header = { 0, 0, 0, 0 };

  std::vector<std::wstring> s = GetVisualLines(true);
  std::vector<std::wstring> c = GetVisualLines(false);

  // PLAY_ENDED
  const int NO_FADE_OUT_DURATION = 550; // Should be a bit longer than gone-ness detection interval
  bool maybe_gone = IsDialogBoxMaybeGone() && (!is_hovered) && (millis - start_millis > NO_FADE_OUT_DURATION) &&
    (caption_state != CAPTION_PLAYING_NO_TIMELINE);
  if (
    (caption_state == CAPTION_PLAY_ENDED) ||
    (caption_state == CAPTION_NOT_PLAYING)
    ) {
    maybe_gone = dialog_box_gone_when_fadingout;
  }
  if (maybe_gone && caption_state == CAPTION_PLAYING) start_millis = 0; // Reveals all
  
  const float o = GetOpacity();

  // Border?
  {
    D3DCOLOR bkcolor = D3DCOLOR_ARGB(int(128 * o), 102, 52, 35), bordercolor = D3DCOLOR_ARGB(int(40 * o), 255, 255, 255);
    if (is_dragging && is_hovered) {
      bordercolor = D3DCOLOR_ARGB(int(255 * o), 255, 255, 192);
    } else if (is_hovered) {
      bordercolor = D3DCOLOR_ARGB(int(255 * o), 192, 192, 192);
    }

    std::wstring probe = g_header1 + L"--";
    g_font->DrawTextW(NULL, probe.c_str(), probe.size(), &rect_header, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    const int pad_left = rect_header.right - rect_header.left;

    DrawBorderedRectangle(x, y, pad_left + this->line_width, s.size() * int(FONT_SIZE * g_ui_scale_factor) + 5, bkcolor, bordercolor);
    visible_height = s.size() * int(FONT_SIZE * g_ui_scale_factor) + 5;
  }

  const int CHAR_PER_SEC = 20; // 是否渐出
  int offset = 0;

  for (unsigned i = 0; i < s.size(); i++) {
    rect.left = x;
    rect.top = y+PADDING_TOP + i * (1 + int(FONT_SIZE * g_ui_scale_factor) );

    std::wstring msg;

    // Measure header width
    msg = g_header1;
    g_font->DrawTextW(NULL, msg.c_str(), msg.size(), &rect_header, DT_CALCRECT, D3DCOLOR_ARGB(int(255 * o), 255, 255, 255));

    // Draw full text
    msg = g_header1 + s[i];
    D3DCOLOR color;
    if (maybe_gone) color = D3DCOLOR_ARGB(int(160 * o), 192, 192, 192);
    else color = D3DCOLOR_ARGB(int(224 * o), 192, 192, 192);
    // Only draw on line[0]
    g_font->DrawTextW(0, msg.c_str(), msg.size(), &rect, DT_NOCLIP, color);

    // Draw proper nouns that are not highlighted
    do_DrawHighlightedProperNouns(rect_header, rect, offset, s[i], D3DCOLOR_ARGB(int(224 * o), 200, 200, 150));

    // Draw current text
    if (i < c.size()) {
      msg = c[i];
      RECT rect_backup = rect;
      rect.left += (rect_header.right - rect_header.left);
      color = D3DCOLOR_ARGB(int(255 * o), 255, 255, 255);
      if (!maybe_gone) {
        g_font->DrawTextW(0, msg.c_str(), msg.size(), &rect, DT_NOCLIP, color);

        // Draw proper nouns that are not highlighted (Very similar to above)
        do_DrawHighlightedProperNouns(rect_header, rect_backup, offset, c[i], D3DCOLOR_ARGB(int(255 * o), 255, 255, 192));
      }
    }
    offset += int(s[i].size());
  }

  // 画出高亮的字符
  if (highlighted_range.first != -999) {
    for (int i = 0; i < highlighted_range.second; i++) {
      int idx = i + highlighted_range.first;
      RECT r = FindBbByCharIdx(idx);// = curr_highlight->first;

      int ix = idx;
      wchar_t ch = GetFullLine()[ix];
      g_font->DrawTextW(0, &ch, 1, &r, DT_NOCLIP, D3DCOLOR_ARGB(int(255 * o), 33, 255, 33));
    }
  }

  if (g_hover_messagebox) g_hover_messagebox->Draw();
  if (is_debug && g_dbg_messagebox) {
    g_dbg_messagebox->CalculateDimension(); // Must be in main thread.
    g_dbg_messagebox->Draw();
  }

  // 恢复先前的渲染状态
  RestoreD3D9RenderState();
}

// TODO
std::wstring RemoveEscapeSequence(const std::wstring& x) {
  std::wstring ret;
  bool in_esc_seq = false;
  for (int i = 0; i<int(x.size()); i++) {
    wchar_t ch = x[i];
    if (ch == L'*') {
      if (!in_esc_seq) i++;

      in_esc_seq = !in_esc_seq;
    }
    else ret.push_back(ch);
  }
  return ret;
}

void ClosedCaption::OnFuncTalk(const char* who, const char* content) {

  g_dialog_box_gone = dialog_box_gone_when_fadingout = false;

  // Fade in
  {
    long long millis = MillisecondsNow();
    fade_end_millis = millis + FADE_IN_MILLIS;
    fade_duration = FADE_IN_MILLIS;
    opacity_start = opacity;
    opacity_end = 1.0f;
    g_last_dialogbox_present_millis = millis;
  }

  proper_noun_ranges.clear();

  std::string who1 = who, content1 = content;
  while ((content1.size() > 0 && (content1.back() == '\n' || content1.back() == '\r'))) content1.pop_back();
  int idx = 0;
  while ((idx + 1 < int(content1.size())) && (content1[idx] == ' ')) idx++;
  content1 = content1.substr(idx);

  printf("[ClosedCaption] OnFuncTalk([%s],[%s])\n", who1.c_str(), content1.c_str());

  // Map back
  {
    bool mapped = false;
    // 从英文找到中文原文

    // 这里有个小型碧油唧：
    // 查story_idx，所依赖的是中文文本。但是为了应对“一条英文对应多条中文”的情况，需要同时用到story_idx与英文。
    // 那么，这里就有了鸡生蛋&蛋生鸡的问题。依赖关系如下：
    //
    // OnFuncTalk ----->  (who_eng, content_eng)  ---+
    //                                               |---> （who_chs, content_chs) ---> story_idx
    //                      story_idx ---------------+
    //
    // 现在看到的curr_story_idx是上一句所对应的idx；现在用于取得下一个story_idx的方法是从all_story_ids中找其接下来的元素。
    //
    int next_story_idx = GuessNextAudioID(curr_story_idx);

    // Attempt 1: Use current Story Idx
    std::pair<std::string, int> wkey1 = std::make_pair(who1, next_story_idx),
      wkey2 = std::make_pair(who1, -999),
      ckey1 = std::make_pair(content1, next_story_idx),
      ckey2 = std::make_pair(content1, -999);

    std::string who1_cand, content1_cand;
    int who_matchid = -999, content_matchid = -999;

    bool who_mapped = false, content_mapped = false;
    // Name of speaker ...
    if (eng2chs_who.find(wkey1) != eng2chs_who.end()) {
      who1_cand = eng2chs_who[wkey1];
      who_matchid = 1;
      who_mapped = true;
    }
    else if (eng2chs_who.find(wkey2) != eng2chs_who.end()) {
      who1_cand = eng2chs_who[wkey2];
      who_matchid = 2;
      who_mapped = true;
    }

    // Content ...
    if (eng2chs_content.find(ckey1) != eng2chs_content.end()) {
      content1_cand = eng2chs_content[ckey1];
      content_matchid = 1;
      content_mapped = true;
    }
    else if (eng2chs_content.find(ckey2) != eng2chs_content.end()) {
      content1_cand = eng2chs_content[ckey2];
      content_matchid = 2;
      content_mapped = true;
    }
    
    if (who_mapped && content_mapped) {
      who1 = who1_cand;
      content1 = content1_cand;
      mapped = true;
      printf("ENG --> CHS found!, Match cases=(%d,%d), remapped([%s],[%s])\n",
        who_matchid, content_matchid,
        who1.c_str(), content1.c_str());
    }
  }

  int len = MultiByteToWideChar(CP_UTF8, 0, content1.c_str(), -1, NULL, NULL);
  int len_who = MultiByteToWideChar(CP_UTF8, 0, who1.c_str(), -1, NULL, NULL);

  wchar_t* wcontent = new wchar_t[len + 1]; // Hopefully this will be enough!
  wchar_t* wwho = new wchar_t[len_who + 1];
  MultiByteToWideChar(CP_UTF8, 0, content1.c_str(), -1, wcontent, len);
  MultiByteToWideChar(CP_UTF8, 0, who1.c_str(), -1, wwho, len_who);

  std::wstring w = wcontent, w1 = wwho;

  speaker = w1;

  delete wcontent;
  delete wwho;

  int ret = FindAlignmentFile(who1.c_str(), content1.c_str());

  if (ret == -999 || ret == -998) {
    millis2word.clear();
    millis2word[std::make_pair(0.0f, 0.0f)] = RemoveEscapeSequence(w);
    printf("[ClosedCaption] alignment file not found or is not okay. length=%lu\n", w.size());
    caption_state = CAPTION_PLAYING_NO_TIMELINE;
  }
  else {
    printf("[ClosedCaption] alignment file found\n");
    caption_state = CAPTION_PLAYING;
  }

  // 无论找没找到对话文本、都标记关键字
  FindKeywordsForHighlighting();
  ComputeBoundingBoxPerChar();
}

// The caller should also call these two functions after calling SetAlignmentFileByAudioID:
//
//  1. ComputeBoundingBoxPerChar();
//  2. FindKeywordsForHighlighting();
//
int ClosedCaption::SetAlignmentFileByAudioID(int id) {
  int ret = -999;
  curr_story_idx = id;
  millis2word.clear();

#if 0
  char path[263]; // NTFS limit: 262 chars
  sprintf_s(path, "%s\\dialog_aligned\\%d_aligned_label.txt", alignment_path.c_str(), id);
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
    ComputeBoundingBoxPerChar();
    thefile.close();
    found = true;
  }
#else
  // 20190716: use zipped file instead of individual files. Much faster!!
  char path[100];
  sprintf_s(path, "%d_aligned_label.txt", id);
  size_t sz;
  char* buf = (char*)mz_zip_reader_extract_file_to_heap(&g_archive_dialog_aligned, path, &sz, 0);
  if (buf != nullptr) {
    printf("[SetAlignmentFileByAudioID] %s = %lu bytes\n", path, sz);
    std::string line;
    int idx = 0;
    while (idx <= int(sz)) {
      if (idx < int(sz) && buf[idx] != '\n') {
        line.push_back(buf[idx]);
      }
      else if ((idx == int(sz)) || (idx < int(sz) && buf[idx] == '\n')) {
        std::istringstream is(line);
        float t0, t1; std::string word;
        is >> t0 >> t1 >> word;
        if (word.size() > 0) {
          millis2word[std::make_pair(t0, t1)] = g_converter.from_bytes(word);
        }
        line.clear();
      }
      idx++;
    }
    delete buf;
    buf = nullptr;
    start_millis = MillisecondsNow();
    ret = 1;
  }
  else {
    printf("[SetAlignmentFileByAudioID] Cannot find [%s] in zipped file\n", path);
  }
#endif

  if (ret != 1) {
    printf("[SetAlignmentFileByAudioID] Input file not good, id=%d\n", id);
    ret = -998;
    caption_state = CAPTION_PLAYING_NO_TIMELINE;
  }
  else {
    caption_state = CAPTION_PLAYING;
  }
  return ret;
}

void ClosedCaption::LoadDummy(const wchar_t* msg) {
  wchar_t s[233];
  if (msg == nullptr) {
     swprintf_s(s, L"Hover/[Right Shift] to reveal; Right mouse click to drag around");
  }
  else swprintf_s(s, msg);

  millis2word.clear();
  for (int i = 0; i < int(wcslen(s)); i++) {
    const float t0 = i * 0.1f, t1 = t0 + 0.1f;
    std::wstring this_one;
    this_one += s[i];
    millis2word[std::make_pair(t0, t1)] = this_one;
  }
  
  caption_state = CAPTION_PLAYING_NO_TIMELINE;
}

void ClosedCaption::FindKeywordsForHighlighting() {
  std::wstring fulltext = GetFullLine();

  // in main thread
  // do_FindKeywordsForHighlighting(fulltext);
  // in worker thread
  EnterCriticalSection(&g_pnl_workq_critsect);
  g_pnl_workqueue.push_back(fulltext);
  LeaveCriticalSection(&g_pnl_workq_critsect);
}

// Thread work-item
void ClosedCaption::do_FindKeywordsForHighlighting(std::wstring s) {
  
  std::vector<bool> pnl_mask;
  pnl_mask.resize(s.size(), false);
  proper_noun_ranges.clear();

  std::set<std::pair<int, int> > next_pnl_ranges;

  for (std::map<std::wstring, std::wstring>::iterator itr =
    proper_names.begin(); itr != proper_names.end(); itr++) {
    std::wstring kw = itr->first;

    int offset = 0;
    while (offset < int(s.size())) {
      size_t occ = s.find(kw, offset);
      if (occ != std::string::npos) {
        next_pnl_ranges.insert(std::make_pair(int(occ), int(kw.size())));
        for (unsigned j = occ; j < occ + int(kw.size()); j++) {
          pnl_mask[j] = true;
        }
        offset = occ + 1;
      }
      else break;
    }
  }

  printf("[FindKeywordsForHighlighting] ");
  for (int i = 0; i<int(pnl_mask.size()); i++) {
    printf("%d", int(pnl_mask[i]));
  }
  printf("\n");

  EnterCriticalSection(&g_pnl_mask_critsect);
  proper_noun_ranges = next_pnl_ranges;
  LeaveCriticalSection(&g_pnl_mask_critsect);
}

void ClosedCaption::LoadSeqDlgIndexFromMemory(void* ptr, int len) {
  char* p = (char*)ptr;
  int idx = 0, num_ety = 0;

  while (idx < len) {
    std::string line = MyGetLineFromMemory((const char*)ptr, &idx, len);
    std::istringstream is(line);
    std::vector<std::string> sp;
    while (is.good()) {
      std::string x;
      is >> x;
      sp.push_back(x);
    }
    line = "";

    std::string content, who; int id;

    if (sp.size() >= 3) {
      id = atoi(sp[0].c_str());
      content = sp.back();
      for (int i = 1; i < int(sp.size()) - 1; i++) {
        if (who != "") who += " ";
        who = who + sp[i];
      }

      all_story_ids.insert(id);
    }

    if (who.size() > 0) {
      index[std::make_pair(who, content)].insert(id);
      num_ety++;
    }
  }
}

// CSV分隔，[ID, Char_CHS, CHS, Char_ENG, ENG_Rev]
void ClosedCaption::LoadEngChsParallelText(const char* fn) {
  assert(0);
  #if 0
  std::ifstream f(fn);
  int num_entries = 0;
  if (f.good()) {
    while (f.good()) {
      int id; std::string char_chs, chs, char_eng, eng_rev;
      std::string line, x;
      std::getline(f, line);
      std::vector<std::string> sp;

      for (int i = 0; i < int(line.size()); i++) {
        if (line[i] == '\t') { // UTF-8里的 \t 也是 0x09
          sp.push_back(x); x = "";
        }
        else {
          x.push_back(line[i]);
        }
      }
      if (x.size() > 0) sp.push_back(x);

      if (sp.size() >= 5) {
        id = atoi(sp[0].c_str());
        char_chs = sp[1];
        chs = sp[2];
        char_eng = sp[3];
        eng_rev = sp[4];
        eng2chs_who[std::make_pair(char_eng, id)] = char_chs;
        eng2chs_who[std::make_pair(char_eng, -999)] = char_chs; // For non-sequence dialogs
        eng2chs_content[std::make_pair(eng_rev, id)] = chs;
        eng2chs_content[std::make_pair(eng_rev, -999)] = chs;
        num_entries++;
        //printf("[%s]->[%s], [%s]->[%s]\n", char_eng.c_str(), char_chs.c_str(), eng_rev.c_str(), chs.c_str());
      }
    }
    f.close();
  }
  printf("%d entries in parallel text [%s] scanned\n", num_entries, fn);
  #endif
}

void ClosedCaption::LoadEngChsParallelTextFromMemory(const char* ptr, int len) {
  int idx = 0, num_entries = 0;
  while (idx < len) {
    int id; std::string char_chs, chs, char_eng, eng_rev;
    std::string line, x;
    
    // Get line
    line = MyGetLineFromMemory(ptr, &idx, len);
    std::vector<std::string> sp = MySplitString(line, '\t');

    if (sp.size() >= 5) {
      id = atoi(sp[0].c_str());
      char_chs = sp[1];
      chs = sp[2];
      char_eng = sp[3];
      eng_rev = sp[4];
      eng2chs_who[std::make_pair(char_eng, id)] = char_chs;
      eng2chs_who[std::make_pair(char_eng, -999)] = char_chs; // For non-sequence dialogs
      eng2chs_content[std::make_pair(eng_rev, id)] = chs;
      eng2chs_content[std::make_pair(eng_rev, -999)] = chs;
      num_entries++;
    }
  }
  printf("%d entries of parallel text scanned from memory\n", num_entries);
}

void ClosedCaption::LoadProperNamesList(const char* fn) {
  std::ifstream f(fn);
  int num_entries = 0;
  if (f.good()) {
    while (f.good()) {
      std::string line, x;
      std::getline(f, line);
      std::vector<std::string> sp = MySplitString(line, '\t');
      
      // MBS -> WCS
      if (sp.size() >= 2) {
        int len_term = MultiByteToWideChar(CP_UTF8, 0, sp[0].c_str(), -1, NULL, NULL);
        wchar_t* wterm = new wchar_t[len_term];
        MultiByteToWideChar(CP_UTF8, 0, sp[0].c_str(), -1, wterm, len_term);
        std::wstring wsterm = wterm;
        delete wterm;

        int len_defn = MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, NULL, NULL);
        wchar_t* wdefn = new wchar_t[len_defn];
        MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, wdefn, len_defn);
        std::wstring wsdefn = wdefn;
        delete wdefn;

        proper_names[wsterm] = wsdefn;
        num_entries++;
        //wprintf(L"%ls->%ls\n", wsterm.c_str(), wsdefn.c_str());
      }
    }
    f.close();
  }
  printf("%d entries in proper names list [%s] scanned\n", num_entries, fn);
}

void ClosedCaption::LoadProperNamesListFromMemory(const char* ptr, int len) {
  int idx = 0, num_entries = 0;
  while (idx < len) {
    std::string x;

    // Get Line
    std::string line = MyGetLineFromMemory(ptr, &idx, len);

    // Split
    std::vector<std::string> sp = MySplitString(line, '\t');
    
    // MBS -> WCS
    if (sp.size() >= 2) {
      int len_term = MultiByteToWideChar(CP_UTF8, 0, sp[0].c_str(), -1, NULL, NULL);
      wchar_t* wterm = new wchar_t[len_term];
      MultiByteToWideChar(CP_UTF8, 0, sp[0].c_str(), -1, wterm, len_term);
      std::wstring wsterm = wterm;
      delete wterm;

      int len_defn = MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, NULL, NULL);
      wchar_t* wdefn = new wchar_t[len_defn];
      MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, wdefn, len_defn);
      std::wstring wsdefn = wdefn;
      delete wdefn;

      proper_names[wsterm] = wsdefn;
      num_entries++;
      //wprintf(L"%ls->%ls\n", wsterm.c_str(), wsdefn.c_str());
    }
  }
  printf("%d entries in proper names list scanned from memory\n", num_entries);
}

int ClosedCaption::FindTextIdxByMousePos(int mx, int my, RECT* p_bb) {
  int ret = -999;
  int local_x = mx - this->x, local_y = my - this->y;
  for (std::map<RECT, int, RectCmp>::iterator itr = bb2char.begin();
    itr != bb2char.end(); itr++) {
    RECT bb = itr->first;
    if (bb.left <= local_x && bb.right >= local_x && bb.top <= local_y && bb.bottom >= local_y) {
      ret = itr->second;
      *p_bb = bb;
      break;
    }
  }
  return ret;
}

void ClosedCaption::UpdateDrag(int mx, int my) {
  if (is_dragging) {
    int dx = mx - mouse_x_start_drag, dy = my - mouse_y_start_drag;
    this->x = x_start_drag + dx;
    this->y = y_start_drag + dy;
  }
}

void ClosedCaption::OnMouseLeftDown(int mx, int my) {
  if (is_hovered == true && is_dragging == false) {
    // Start dragging
    is_dragging = true;
    mouse_x_start_drag = mx;
    mouse_y_start_drag = my;
    x_start_drag = this->x;
    y_start_drag = this->y;
    is_dragging = true;
    UpdateDrag(mx, my);
  }
}

void ClosedCaption::OnMouseLeftUp(int mx, int my) {
  if (is_dragging) {
    UpdateDrag(mx, my);
    is_dragging = false;
  }
}

void ClosedCaption::UpdateMousePos(int mx, int my) {

  RECT bb;
  int ch_idx = FindTextIdxByMousePos(mx, my, &bb);
#if 0
  printf("[UpdateMousePos] x=%d y=%d ch_idx=%d pns:", x, y, ch_idx);
  for (std::pair<int, int> p : proper_noun_ranges) {
    printf("(%d,%d) ", p.first, p.second);
  }
  printf("\n");
#endif
  if (ch_idx != -999 && (ch_idx != last_highlight_idx)) {
    std::wstring popup_txt = L"";
    // Any substring found in the list?

    EnterCriticalSection(&g_pnl_mask_critsect);
    for (std::set<std::pair<int, int> >::iterator itr = proper_noun_ranges.begin(); itr != proper_noun_ranges.end(); itr++) {
      highlighted_range.first = -999;
      highlighted_range.second = 0;


      // Will find all matches
      if (itr->first <= ch_idx && itr->first + itr->second > ch_idx) {
        const int offset = itr->first, len = itr->second;
        highlighted_range.first = offset;
        highlighted_range.second = len;

        std::wstring fulltext = GetFullLine();
        std::wstring key = fulltext.substr(itr->first, itr->second);
        std::wstring desc = proper_names[key];
        std::wstring txt = ToWstring("【") + key + ToWstring("】\n") + desc;
        
        if (popup_txt.size() > 0)
          popup_txt += ToWstring("；");
        popup_txt += txt;

      }
    }
    LeaveCriticalSection(&g_pnl_mask_critsect);

    if (popup_txt.size() > 0) {
      g_hover_messagebox->SetText(popup_txt, int(320 * g_ui_scale_factor));
      g_hover_messagebox->x = bb.left + this->x;
      g_hover_messagebox->y = bb.top + int(FONT_SIZE * g_ui_scale_factor) + MOUSE_Y_DELTA + this->y;
      g_hover_messagebox->CalculateDimension(); // Must be in main thread
    }
    else {
      g_hover_messagebox->Hide();
    }
  } else if (ch_idx == -999) {
    highlighted_range.first = -999;
    highlighted_range.second = 0;
    g_hover_messagebox->Hide();
  }

  is_hovered = IsMouseHover(mx, my);
  if (is_hovered) Hover();

  last_highlight_idx = ch_idx;
}

void ClosedCaption::Hover() {
  is_hovered = true;
  g_dialog_box_gone = false;
  dialog_box_gone_when_fadingout = false;

  opacity_start = opacity_end = opacity = 1.0f;
  fade_end_millis = MillisecondsNow();
}

void ClosedCaption::AutoPosition(int win_w, int win_h) {
  RECT r = g_curr_dialogbox_rect;
  //int y = r.top + 5;
  int y = 5; // Top display?
  if (y > win_h - 32) y = win_h - 32;
  int x = win_w / 2 - line_width / 2;
  this->x = x;
  this->y = y;
}

void ClosedCaption::FadeIn() {
  if (opacity >= 1.0f) return;
  long long millis = MillisecondsNow();
  if (millis < fade_end_millis && opacity_end == 1.0f) return;
  printf("[FadeIn]\n");
  opacity_start = opacity;
  fade_end_millis = millis + FADE_IN_MILLIS;
  fade_duration = FADE_IN_MILLIS;
  opacity_end = 1.0f;
}

void ClosedCaption::FadeOut() {
  if (opacity <= 0.0f) return;
  long long millis = MillisecondsNow();
  if (millis < fade_end_millis && opacity_end == 0.0f) return;

  opacity_start = opacity;
  fade_end_millis = millis + FADE_OUT_MILLIS;
  fade_duration = FADE_OUT_MILLIS;
  opacity_end = 0.0f;
}

// ==========================================
//  My Message Box
// ==========================================

void MyMessageBox::StaticInit() {
  
}

void MyMessageBox::CalculateDimension() {
  EnterCriticalSection(&g_calc_dimension);
  lines.clear();
  std::wstring line, curr_word;
  for (int i = 0; i <= int(message.size()); i++) {
    bool is_push = false;
    wchar_t ch = L'\0', ch_next = L'\0';
    if (i < int(message.size())) {
      ch = message[i];
      if (i + 1 < int(message.size())) {
        ch_next = message[i + 1];
      }
    }

    RECT rect;
    g_font->DrawTextW(NULL, line.c_str(), wcslen(line.c_str()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    const int line_w = rect.right - rect.left;

    g_font->DrawTextW(NULL, curr_word.c_str(), wcslen(curr_word.c_str()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    const int word_w = rect.right - rect.left;

    if (word_w > w) {
      lines.push_back(curr_word.substr(0, int(curr_word.size()) - 1));
      curr_word = curr_word.back();
    }
    else if ((ch == L'\n' || ch == L'\r')) {
      curr_word = curr_word + ch;
      if (line.size() > 0) line = line + L" ";
      line = line + curr_word;
      lines.push_back(line);
      curr_word = line = L"";
    } 
    else if ((ch == L'\\') && (ch_next == L'n' || ch_next == L'r')) {
      i++;
      if (line.size() > 0) line = line + L" ";
      line = line + curr_word;
      lines.push_back(line);
      curr_word = line = L"";
    }
    else if (ch == L' ' || i == message.size()) {
      if (line_w + word_w > w) {
        lines.push_back(line);
        line = curr_word;
        curr_word = L"";
      }
      else {
        if (line.size() > 0) line = line + L" ";
        line = line + curr_word;
        curr_word = L"";
      }

      if (i == message.size()) {
        lines.push_back(line);
      }
    }
    else {
      curr_word.push_back(ch);
    }
    /*
    curr_word += message[i];
    line = line + message[i];
    line_peek = line + message[i + 1];
    g_font->DrawTextW(NULL, line_peek.c_str(), wcslen(line_peek.c_str()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    if (rect.right - rect.left > w) is_push = true;
    */
    
  }

  h = int(FONT_SIZE * g_ui_scale_factor * lines.size());
  LeaveCriticalSection(&g_calc_dimension);
}

void MyMessageBox::SetText(const std::wstring& msg, int width_limit) {
  message = msg;
  w = width_limit;
  x = y = h = 0;
}

void MyMessageBox::Draw(D3DCOLOR bkcolor, D3DCOLOR bordercolor, D3DCOLOR fontcolor) {
  int dx = x, dy = y;
  switch (gravity) {
    case 2: case 6: dy -= h / 2; break; // Vertically centered
    case 3: case 4: case 5: dy -= h; break; // Vertically aligned to bottom
  }
  switch (gravity) {
    case 0: case 4: dx -= h / 2; break; // Horizontally centered
    case 1: case 2: case 3: dx -= h; break; // Horizontally aligned to right
  }
  DrawBorderedRectangle(dx, dy, w, h, bkcolor, bordercolor);
  for (unsigned i = 0; i < lines.size(); i++) {
    std::wstring& line = lines[i];
    RECT rect;
    g_font->DrawTextW(NULL, line.c_str(), wcslen(line.c_str()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
    int txt_width = rect.right - rect.left;
    if (text_align < 0) {
      rect.left = dx;
    }
    else if (text_align == 0) {
      rect.left = dx + w / 2 - txt_width / 2;
    }
    else {
      rect.left = dx + w - txt_width;
    }
    rect.top = dy + i * int(FONT_SIZE * g_ui_scale_factor);
    g_font->DrawTextW(NULL, line.c_str(), wcslen(line.c_str()), &rect, DT_NOCLIP, fontcolor);
  }
}

void MyMessageBox::Draw() {
  D3DCOLOR bkcolor = D3DCOLOR_ARGB(128, 102, 52, 35), bordercolor = D3DCOLOR_ARGB(32, 255, 255, 255), fontcolor = D3DCOLOR_ARGB(255, 255, 255, 255);
  Draw(bkcolor, bordercolor, fontcolor);
}

void VideoSubtitles::Draw() {
  // create MessageBox if not exists
  if (GetCurrString() == L"") return;
  if (state == SUBTITLES_PLAYING) {
    if (messagebox) {
      BackupD3D9RenderState();
      float o = GetCurrOpacity();
      D3DCOLOR bkcolor = D3DCOLOR_ARGB(int(128 * o), 102, 52, 35), bordercolor = D3DCOLOR_ARGB(int(32 * o), 255, 255, 255), fontcolor = D3DCOLOR_ARGB(int(255 * o), 255, 255, 255);
      messagebox->Draw(bkcolor, bordercolor, fontcolor);
      RestoreD3D9RenderState();
    }
  }
}

void VideoSubtitles::Start() {
  start_millis = long(MillisecondsNow());
  state = SUBTITLES_PLAYING;
  do_SetIdx(0);
}

void VideoSubtitles::Stop() {
  state = SUBTITLES_NOT_STARTED;
}

void VideoSubtitles::Update() {
  if (state == SUBTITLES_NOT_STARTED || state == SUBTITLES_ENDED) return;

  const long long millis = MillisecondsNow();
  long long deadline = 0, elapsed = 0;
  switch (state) {
  case SUBTITLES_NOT_STARTED: break;
  case SUBTITLES_PLAYING:
    elapsed = millis - start_millis;
    while (curr_idx + 1 < int(millisecs.size()) &&
           elapsed > millisecs[curr_idx+1]) {
      curr_idx++;
    }
    if (curr_idx == int(millisecs.size() - 1)) Stop();
    else {
      do_SetIdx(curr_idx);
    }
    break;
  case SUBTITLES_ENDED:
    break;
  }
}

void VideoSubtitles::do_SetIdx(int idx) {
  curr_idx = idx;
  std::wstring txt = L"";
  if (curr_idx >= 0 && curr_idx < int(millisecs.size())) {
    txt = subtitles[curr_idx];
  } else if (curr_idx >= int(millisecs.size())) {
    state = SUBTITLES_ENDED;
  }
  if (messagebox == nullptr) {
    messagebox = new MyMessageBox();
  }
  messagebox->SetText(txt, int(800 * g_ui_scale_factor));
  messagebox->w = int(800 * g_ui_scale_factor);
  messagebox->h = int(64 * g_ui_scale_factor);
  messagebox->x = int(g_win_w / 2 - messagebox->w / 2);
  messagebox->y = int(g_win_h - 10 * g_ui_scale_factor);
  messagebox->text_align = 0;
  messagebox->gravity = 4;
  messagebox->CalculateDimension();
}

void VideoSubtitles::LoadFromMemory(const char* ptr, int len) {
  subtitles.clear();
  millisecs.clear();

  int idx = 0, num_entries = 0;
  while (idx < len) {
    std::string x;

    // Get Line
    std::string line = MyGetLineFromMemory(ptr, &idx, len);
    if (line[0] == '#') continue;

    // Split
    std::vector<std::string> sp = MySplitString(line, '\t');

    // MBS -> WCS
    if (sp.size() == 1) sp.push_back("");

    if (sp.size() >= 2) {
      int millis = std::atoi(sp[0].c_str());
      millisecs.push_back(millis);

      int len_defn = MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, NULL, NULL);
      wchar_t* wdefn = new wchar_t[len_defn];
      MultiByteToWideChar(CP_UTF8, 0, sp[1].c_str(), -1, wdefn, len_defn);
      std::wstring wsdefn = wdefn;
      delete wdefn;

      subtitles.push_back(wsdefn);
      num_entries++;
    }
  }

  Init(subtitles, millisecs);

  printf("Video subtitles with %d entries scanned from memory\n", num_entries);
}

std::wstring VideoSubtitles::GetCurrString() {
  if (curr_idx < 0 || curr_idx >= int(subtitles.size())) return L"";
  else return subtitles[curr_idx];
}

float VideoSubtitles::GetCurrOpacity() {
  const int FADE_MILLIS = 300;
  long elapsed = long(MillisecondsNow() - start_millis);
  if (curr_idx >= 0 && curr_idx < int(millisecs.size())) {
    int dist_begin = elapsed - millisecs[curr_idx];
    int dist_end = (curr_idx < int(millisecs.size() - 1)) ? millisecs[curr_idx + 1] - elapsed : 1000000007;
    float opacity = 1.0f;
    if (dist_end < FADE_MILLIS) {
      opacity = (1.0f * dist_end) / float(FADE_MILLIS);
    }
    if (dist_begin < FADE_MILLIS) {
      opacity = (1.0f * dist_begin) / float(FADE_MILLIS);
    }
    return opacity;
  }
  return 0.0f;
}