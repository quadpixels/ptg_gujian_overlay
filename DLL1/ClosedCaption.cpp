#include "ClosedCaption.h"

#include <codecvt>
#include <d3dx9.h>
#include <d3d9.h>
#include "miniz.h"

// in dll1.cpp
extern std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > g_converter;
extern long long MillisecondsNow();
extern LPD3DXFONT g_font;
extern int FONT_SIZE;
extern MyMessageBox* g_hover_messagebox;
extern IDirect3DDevice9* g_pD3DDevice;

IDirect3DVertexBuffer9* g_vert_buf;
IDirect3DStateBlock9* g_pStateBlock;

extern mz_zip_archive g_archive_dialog_aligned;

struct MyD3D9RenderState {
  DWORD alpha_blend_enable, blend_op, blend_op_alpha, src_blend, dest_blend, lighting, cull_mode;
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
}

// ========================= HELPER ==============================
void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor) {
  void* pVoid;
  const float Z = -1.0f;
  // 画背景
  CustomVertex quad_bk[] =
  {
    { x,   y,   Z, 1.0f, bkcolor, },
    { x + w, y,   Z, 1.0f, bkcolor, },
    { x,   y + h, Z, 1.0f, bkcolor, },
    { x + w, y + h, Z, 1.0f, bkcolor, },
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
    { x,   y,   Z, 1.0f, bordercolor, },
    { x + w, y,   Z, 1.0f, bordercolor, },

    { x + w, y,   Z, 1.0f, bordercolor, },
    { x + w, y + h, Z, 1.0f, bordercolor, },

    { x + w, y + h, Z, 1.0f, bordercolor, },
    { x,   y + h, Z, 1.0f, bordercolor, },

    { x,   y + h, Z, 1.0f, bordercolor, },
    { x,   y,   Z, 1.0f, bordercolor, },
  };

  g_vert_buf->Lock(0, 0, (void**)(&pVoid), 0);
  memcpy(pVoid, quad, sizeof(quad));
  g_vert_buf->Unlock();

  g_pD3DDevice->SetStreamSource(0, g_vert_buf, 0, sizeof(CustomVertex));
  g_pD3DDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, 0, 8);
}
// ===============================================================

int ClosedCaption::PADDING_LEFT = 0;
int ClosedCaption::PADDING_TOP = 3;
int ClosedCaption::MOUSE_Y_DELTA = 24;

ClosedCaption::ClosedCaption() {
  visible = true;
  line_width = 640;
  visible_height = 16;
  x = 2;
  y = 29;
}

std::wstring ClosedCaption::GetCurrLine() {

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

std::wstring ClosedCaption::GetFullLine() {
  std::wstring ret = speaker + L"：";
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
    RECT rect, rect_header;
    rect.left = left; rect.top = y0 + i * (1 + FONT_SIZE);
    wchar_t msg[1024];

    // Header
    if (i == 0) wsprintf(msg, L"[ptg]>");
    else wsprintf(msg, L"     >");
    g_font->DrawTextW(NULL, msg, wcslen(msg), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    int last_x = rect.right;
    if (i == 0) wsprintf(msg, L"[ptg]>%ls", s[i].c_str());
    else wsprintf(msg, L"     >%ls", s[i].c_str());
    const int HEADER_LEN = 6;
    for (int j = HEADER_LEN; j < wcslen(msg); j++) {
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

void ClosedCaption::do_DrawHighlightedProperNouns(RECT rect_header, RECT rect, const int curr_text_offset, const std::wstring line, D3DCOLOR color) {
  const int lb = 0, ub = 0 + line.size() - 1;
  for (const std::pair<int, int> x : proper_noun_ranges) {
    RECT rect_header1 = rect_header, rect1 = rect;
    int lb1 = x.first - curr_text_offset, ub1 = x.first + x.second - 1 - curr_text_offset; // 要减去该行所在的offset

    if (!((ub < lb1) || (ub1 < lb))) {
      int lb_vis = max(lb1, lb), ub_vis = min(ub, ub1);
      //printf("Curr Text:    (lb,  ub)=(%d,%d)\n", lb, ub);
      //printf("Curr Segment: (lb1,ub1)=(%d,%d)\n", lb1, ub1);
      std::wstring preceding = line.substr(0, lb_vis - lb);
      std::wstring content = line.substr(lb_vis, ub_vis - lb_vis + 1);
      std::wstring teststr = L"     >" + preceding;
      g_font->DrawTextW(NULL, teststr.c_str(), teststr.size(), &rect_header1, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
      rect1.left += rect_header1.right - rect_header1.left;
      g_font->DrawTextW(NULL, content.c_str(), int(content.size()), &rect1, DT_NOCLIP, color);
    }
  }
}

void ClosedCaption::do_DrawBorder(int x, int y, int w, int h) {
  
}

void ClosedCaption::Draw() {
  if (!visible) return;

  // 保存先前的渲染状态
  BackupD3D9RenderState();

  RECT rect = { 0, 0, 0, 0 };
  RECT rect_header = { 0, 0, 0, 0 };

  std::vector<std::wstring> s = GetVisualLines(true);
  std::vector<std::wstring> c = GetVisualLines(false);

  // Border?
  {
    D3DCOLOR bkcolor = D3DCOLOR_ARGB(128, 102, 52, 35), bordercolor = D3DCOLOR_XRGB(255, 255, 255);
    if (is_dragging && is_hovered) {
      bordercolor = D3DCOLOR_XRGB(255, 32, 32);
    } else if (is_hovered) {
      bordercolor = D3DCOLOR_XRGB(255, 255, 0);
    }
    DrawBorderedRectangle(x, y, 90 + this->line_width, s.size() * FONT_SIZE + 5, bkcolor, bordercolor);
    visible_height = s.size() * FONT_SIZE + 5;
  }

  const int CHAR_PER_SEC = 20; // 是否渐出
  long long millis = MillisecondsNow();
  int offset = 0;

  for (int i = 0; i < s.size(); i++) {
    rect.left = x;
    rect.top = y+PADDING_TOP + i * (1 + FONT_SIZE);

    std::wstring msg;

    // Measure header width
    if (i == 0) msg = L"[ptg]>";
    else msg = L"     >";
    g_font->DrawTextW(NULL, msg.c_str(), msg.size(), &rect_header, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    // Draw full text
    // TODO: 画出高亮的专有名词
    if (i == 0) msg = L"[ptg]>" + s[i];
    else msg = L"     >" + s[i];
    D3DCOLOR color = D3DCOLOR_ARGB(150, 150, 150, 150);
    g_font->DrawTextW(0, msg.c_str(), msg.size(), &rect, DT_NOCLIP, color);

    // Draw proper nouns that are not highlighted
    do_DrawHighlightedProperNouns(rect_header, rect, offset, s[i], D3DCOLOR_ARGB(150, 200, 200, 150));

    // Draw current text
    if (i < c.size()) {
      msg = c[i];
      RECT rect_backup = rect;
      rect.left += (rect_header.right - rect_header.left);
      color = D3DCOLOR_ARGB(255, 255, 255, 255);
      g_font->DrawTextW(0, msg.c_str(), msg.size(), &rect, DT_NOCLIP, color);

      // Draw proper nouns that are not highlighted (Very similar to above)
      do_DrawHighlightedProperNouns(rect_header, rect_backup, offset, c[i], D3DCOLOR_ARGB(255, 255, 255, 192));
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
      g_font->DrawTextW(0, &ch, 1, &r, DT_NOCLIP, D3DCOLOR_ARGB(255, 33, 255, 33));
    }
  }

  if (g_hover_messagebox) g_hover_messagebox->Draw();

  for (const RECT& r : highlight_rects) {
    DrawBorderedRectangle(r.left, r.top, (r.right - r.left), (r.bottom - r.top), D3DCOLOR_ARGB(0, 0, 0, 0), D3DCOLOR_ARGB(255, 32, 255, 32));
  }

  // 恢复先前的渲染状态
  RestoreD3D9RenderState();
}

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
    while (idx <= sz) {
      if (idx < sz && buf[idx] != '\n') {
        line.push_back(buf[idx]);
      }
      else if ((idx == sz) || (idx < sz && buf[idx] == '\n')) {
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
    ComputeBoundingBoxPerChar();
    ret = true;
  }
  else {
    printf("[SetAlignmentFileByAudioID] Cannot find [%s] in zipped file\n", path);
  }
#endif

  if (ret != true) {
    printf("[SetAlignmentFileByAudioID] Input file not good, id=%d\n", id);
    ret = -998;
  }
  return ret;
}

void ClosedCaption::LoadDummy() {
  wchar_t s[] = L"蓬莱翻译组当真是十分美妙";
  //proper_names[L"蓬莱翻译组"] = L"[péng lái fān yì zǔ] Penglai Translation Group (PTG)";

  millis2word.clear();
  for (int i = 0; i < int(wcslen(s)); i++) {
    const float t0 = i * 0.3f, t1 = t0 + 0.3f;
    std::wstring this_one;
    this_one += s[i];
    millis2word[std::make_pair(t0, t1)] = this_one;
  }
  start_millis = MillisecondsNow();
  ComputeBoundingBoxPerChar();
  FindKeywordsForHighlighting();
}

void ClosedCaption::FindKeywordsForHighlighting() {
  std::wstring fulltext = GetFullLine();

  proper_noun_mask.clear();
  proper_noun_mask.resize(fulltext.size(), false);

  proper_noun_ranges.clear();

  for (std::map<std::wstring, std::wstring>::iterator itr =
    proper_names.begin(); itr != proper_names.end(); itr++) {
    std::wstring kw = itr->first;

    int offset = 0;
    while (offset < int(fulltext.size())) {
      size_t occ = fulltext.find(kw, offset);
      if (occ != std::string::npos) {
        proper_noun_ranges.insert(std::make_pair(int(occ), int(kw.size())));
        for (int j = occ; j < occ + int(kw.size()); j++) {
          proper_noun_mask[j] = true;
        }
        offset = occ + 1;
      }
      else break;
    }
  }

  printf("[FindKeywordsForHighlighting] ");
  for (int i = 0; i<int(proper_noun_mask.size()); i++) {
    printf("%d", int(proper_noun_mask[i]));
  }
  printf("\n");
}

void ClosedCaption::LoadProperNamesList(const char* fn) {
  std::ifstream f(fn);
  int num_entries = 0;
  if (f.good()) {
    while (f.good()) {
      int term, defn;
      std::string line, x;
      std::getline(f, line);
      std::vector<std::string> sp;
      for (int i = 0; i < line.size(); i++) {
        if (line[i] == '\t') {
          sp.push_back(x); x = "";
        }
        else {
          x.push_back(line[i]);
        }
      }
      if (x.size() > 0) sp.push_back(x);

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
        wprintf(L"%ls->%ls\n", wsterm.c_str(), wsdefn.c_str());
      }
    }
    f.close();
  }
  printf("%d entries in proper names list [%s] scanned\n", num_entries, fn);
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
    // 是否与词典重合
    for (std::set<std::pair<int, int> >::iterator itr = proper_noun_ranges.begin(); itr != proper_noun_ranges.end(); itr++) {
      highlighted_range.first = -999;
      highlighted_range.second = 0;
      if (itr->first <= ch_idx && itr->first + itr->second > ch_idx) {
        const int offset = itr->first, len = itr->second;
        highlighted_range.first = offset;
        highlighted_range.second = len;

        std::wstring fulltext = GetFullLine();
        std::wstring key = fulltext.substr(itr->first, itr->second);
        std::wstring desc = proper_names[key];
        std::wstring txt = desc;
        g_hover_messagebox->SetText(txt, 250);
        g_hover_messagebox->x = bb.left + this->x;
        g_hover_messagebox->y = bb.top + FONT_SIZE + MOUSE_Y_DELTA + this->y;

        break;
      }
    }
  } else if (ch_idx == -999) {
    highlighted_range.first = -999;
    highlighted_range.second = 0;
    g_hover_messagebox->Hide();
  }

  is_hovered = IsMouseHover(mx, my);

  last_highlight_idx = ch_idx;
}

// ==========================================
//  My Message Box
// ==========================================

void MyMessageBox::StaticInit() {
  
}

void MyMessageBox::CalculateDimension() {
  lines.clear();
  std::wstring line;
  for (int i = 0; i <= message.size(); i++) {

    std::wstring line_peek;
    bool is_push = false;

    if (i < message.size()) {
      line = line + message[i];
      line_peek = line + message[i + 1];
      RECT rect;
      g_font->DrawTextW(NULL, line_peek.c_str(), wcslen(line_peek.c_str()), &rect, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));
      if (rect.right - rect.left > w) is_push = true;
    }
    else {
      if (line.size() > 0)
        is_push = true;
    }

    if (is_push == true) { // 换行
      lines.push_back(line);
      line = L"";
      h += FONT_SIZE;
    }
  }
}

void MyMessageBox::SetText(const std::wstring& msg, int width_limit) {
  message = msg;
  w = width_limit;
  x = y = h = 0;
  CalculateDimension();
}

void MyMessageBox::Draw() {
  {
    D3DCOLOR bkcolor = D3DCOLOR_ARGB(128, 102, 52, 35), bordercolor = D3DCOLOR_XRGB(255, 255, 255);
    DrawBorderedRectangle(x, y, w, h, bkcolor, bordercolor);
  }

  for (int i = 0; i < lines.size(); i++) {
    std::wstring& line = lines[i];
    RECT rect;
    rect.left = x;
    rect.top = y + i * FONT_SIZE;
    g_font->DrawTextW(NULL, line.c_str(), wcslen(line.c_str()), &rect, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 255, 255));
  }
}