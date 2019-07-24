#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include <d3d9.h> // for RECT
#include <d3dx9.h> // for FONT
#include <synchapi.h>
#include <unordered_map>
#include <unordered_set>

#include "ClosedCaption.h"
#include "config.h"

extern ClosedCaption g_ClosedCaption;
extern void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor);
extern CRITICAL_SECTION g_critsect_rectlist;
extern LPD3DXFONT g_font;
extern IDirect3DSurface9* g_pSurfaceCopy;
extern LPD3DXBUFFER g_pSurfaceBuf;
extern PtgOverlayConfig g_config;

extern void BackupD3D9RenderState();
extern void RestoreD3D9RenderState();

// 每个Template 可以有自己的 scale
struct MatchTemplate {
  cv::Mat* mat;
  float scale;
  std::wstring caption;
  int direction; // 0123 = URDL
  MatchTemplate() { 
    scale = 0.5f;  // Use 0.5x resolution by default
    direction = 3;
  }
};

// 屏幕内容检测用
cv::Mat g_surface_mat;
std::vector<MatchTemplate> g_templates;
HANDLE g_detector_thd_handle;
DWORD  g_detector_thd_id;
volatile bool g_detector_done = false;
volatile bool g_detector_sleeping = true;
DWORD WINAPI MyThreadFunction(LPVOID lpParam);
void DoDetect(unsigned char* chr, const int len);
void DoCheckMatchesDisappear(unsigned char* chr, const int len);

struct MatchResult {
  int idx;
  RECT rect_scaled;
  RECT rect_orig;
  MatchResult(int _idx, RECT _scaled, RECT _orig) {
    idx = _idx;
    rect_scaled = _scaled;
    rect_orig = _orig;
  }
};
std::vector<MatchResult> highlight_rects;

unsigned char* g_img_data;
int g_img_len;
int g_detector_mode; // 1: detect, 2: remove existing

void WakeDetectorThread(unsigned char* ch, const int len, const int mode) {
  g_img_data = ch;
  g_img_len = len;
  g_detector_mode = mode;

  MemoryBarrier();

  g_detector_sleeping = false;
}

DWORD WINAPI MyThreadFunction(LPVOID lpParam) {
  
  printf("[Thd %d] MyThreadFunction\n", int(GetCurrentThreadId()));

  while (g_detector_done == false) {
    while (g_detector_sleeping) {
      Sleep(50);
    }

    switch (g_detector_mode) {
    case 1:
      DoDetect(g_img_data, g_img_len);
      break;
    case 2:
      DoCheckMatchesDisappear(g_img_data, g_img_len);
      break;
    }

    g_detector_sleeping = true;
  }

  return S_OK;
}

// About half second using debug library
void DoDetect(unsigned char* chr, const int len) {
  std::vector<struct MatchResult> next_rect;
  printf(">>> [DoDetect]\n");
  //g_surface_mat = cv::Mat(w, h, CV_8UC4, ch).t();

  cv::Mat raw_data_mat(1, len, CV_8UC1, chr);
  cv::Mat tmp = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  raw_data_mat.release();

  std::unordered_map<float, cv::Mat*> resized;

  cv::Mat result;
  cv::TemplateMatchModes mode = cv::TM_CCOEFF_NORMED;

  for (int i = 0; i < int(g_templates.size()); i++) {

    const cv::Mat& m = *(g_templates[i].mat);

    const float scale = g_templates[i].scale;

    cv::Mat* r = nullptr;
    if (resized.find(scale) == resized.end()) {
      r = new cv::Mat();
      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
      resized[scale] = r;
    }

    r = resized[scale];

    cv::matchTemplate(*r, m, result, mode);
    cv::Point min_loc, max_loc;
    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc, cv::Mat());

    cv::Point ans;
    double metric;
    bool found = false;

    switch (mode) {
    case cv::TM_SQDIFF: case cv::TM_SQDIFF_NORMED:
      ans = min_loc;
      metric = min_val;
      break;
    default:
      ans = max_loc;
      metric = max_val;
      if (mode == cv::TM_CCOEFF_NORMED) {
        if (metric > 0.8) {
          found = true;
        }
      }
      break;
    }

    printf("   [DoDetect] %s, scale=%g, Min Loc: (%d,%d); min&max val:(%g,%g) found=%d\n",
      g_templates[i].caption.c_str(), g_templates[i].scale,
      int(ans.x / scale), int(ans.y / scale),
      min_val, max_val,
      found);

    if (found == true) {
      RECT r_scaled, r_orig;
      r_orig.left = ans.x / scale;
      r_orig.top = ans.y / scale;
      r_orig.right = r_orig.left + (m.cols / scale);
      r_orig.bottom = r_orig.top + (m.rows / scale);

      r_scaled.left = ans.x;
      r_scaled.top = ans.y;
      r_scaled.right = r_scaled.left + m.cols;
      r_scaled.bottom = r_scaled.top + m.rows;

      struct MatchResult mr(i, r_scaled, r_orig);
      next_rect.push_back(mr);
    }
  }

  tmp.release();

  printf("<<< [DoDetect]\n");

  for (std::pair<float, cv::Mat*> p : resized) {
    p.second->release();
    delete p.second;
  }

  EnterCriticalSection(&g_critsect_rectlist);
  highlight_rects.clear();
  highlight_rects = next_rect;
  LeaveCriticalSection(&g_critsect_rectlist);
}

void DoCheckMatchesDisappear(unsigned char* chr, const int len) {
  cv::Mat raw_data_mat(1, len, CV_8UC1, chr);
  cv::Mat tmp = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  raw_data_mat.release();
  assert(tmp.type() == CV_8UC3);
  
  std::unordered_map<float, cv::Mat*> resized;
  std::unordered_set<int> victim_idxes;

  int idx = 0;
  for (const MatchResult& mr : highlight_rects) {
    const MatchTemplate& mt = g_templates[mr.idx];
    const float scale = mt.scale;

    // Obtain texture that matches the template's scale
    cv::Mat* r = nullptr;
    if (resized.find(scale) == resized.end()) {
      r = new cv::Mat();
      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
      resized[scale] = r;
    }
    r = resized[scale];

    const int resized_w = mr.rect_scaled.right - mr.rect_scaled.left;
    const int resized_h = mr.rect_scaled.bottom - mr.rect_scaled.top;

    cv::Mat cropped(resized_h, resized_w, tmp.type());
    for (int x = mr.rect_scaled.left, x0 = 0; x < mr.rect_scaled.right; x++, x0++) {
      for (int y = mr.rect_scaled.top, y0 = 0; y < mr.rect_scaled.bottom; y++, y0++) {
        //cropped.row(y0).col(x0) = r->row(y).col(x);
        cropped.at<cv::Vec3b>(y0, x0) = r->at<cv::Vec3b>(y, x);
      }
    }

    cv::Mat result;
    cv::matchTemplate(cropped, *(mt.mat), result, cv::TM_CCOEFF_NORMED);

    cv::Point min_loc, max_loc;
    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc, cv::Mat());
    double metric = max_val;
    printf("[%d], metric=%g\n", idx, metric);
    if (metric < 0.8) {
      victim_idxes.insert(idx);
    }

    cropped.release();
    idx++;
  }

  for (std::pair<float, cv::Mat*>p : resized) {
    p.second->release();
    delete p.second;
  }

  EnterCriticalSection(&g_critsect_rectlist);
  std::vector<MatchResult> result_next;
  for (int i = 0; i<int(highlight_rects.size()); i++) {
    if (victim_idxes.find(i) == victim_idxes.end()) {
      result_next.push_back(highlight_rects[i]);
    }
  }

  tmp.release();
  highlight_rects = result_next;
  LeaveCriticalSection(&g_critsect_rectlist);
}

void LoadImagesAndInitDetectThread() {

  InitializeCriticalSection(&g_critsect_rectlist);

  // Read Image List
  {
    const std::string image_list = g_config.root_path + "\\TemplateList.txt";
    
    std::string dir_name = image_list;
    int idx = int(dir_name.size() - 1);
    while (idx > 0 && dir_name[idx] != '\\') idx--;
    dir_name = dir_name.substr(0, idx);

    std::ifstream f(image_list);
    if (f.good()) {
      while (f.good()) {
        std::string file_name, caption;
        std::string line, x;
        float scale = 0.5;

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

        if (sp.size() >= 2) {
          MatchTemplate t;
          file_name = dir_name + "\\" + sp[0]; caption = sp[1];

          if (sp.size() >= 3) {
            scale = std::stof(sp[2]);
            if (scale < 0 || scale > 1) scale = 0.5f;
            t.scale = scale;
          }

          if (sp.size() >= 4) {
            t.direction = std::stoi(sp[3]);
          }

          cv::Mat tmp = cv::imread(file_name.c_str(), cv::IMREAD_COLOR);
          t.mat = new cv::Mat();
          cv::resize(tmp, *(t.mat), cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

          // MBS to WCS
          int len_caption = MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, NULL, NULL);
          wchar_t* wcaption = new wchar_t[len_caption];
          MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, wcaption, len_caption);
          t.caption = wcaption;
          delete wcaption;

          printf("Loaded template [%s] = %dx%d, resized=%dx%d\n",
            file_name.c_str(), tmp.cols, tmp.rows, t.mat->cols, t.mat->rows);
          g_templates.push_back(t);
        }
      }
    }
  }

  g_detector_thd_handle = CreateThread(
    nullptr,
    0,
    MyThreadFunction,
    nullptr,
    0,
    &g_detector_thd_id
  );

}

void DrawHighlightRects() {
  EnterCriticalSection(&g_critsect_rectlist);
  BackupD3D9RenderState();

  for (const MatchResult& mr : highlight_rects) {
    struct MatchTemplate t = g_templates[mr.idx];
    const std::wstring& caption = t.caption;
    
    // Measure text width
    RECT cr; // CR = Caption Rect
    g_font->DrawTextW(NULL, caption.c_str(), caption.size(), &cr, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    RECT r = mr.rect_orig;
    DrawBorderedRectangle(r.left, r.top, (r.right - r.left), (r.bottom - r.top), D3DCOLOR_ARGB(0, 0, 0, 0), D3DCOLOR_ARGB(255, 32, 255, 32));

    RECT d = { 0,0,0,0 };
    
    const int dir = t.direction;
    if (dir == 0 || dir == 2) { // V-align
      d.left = (r.left + r.right) / 2 - (cr.right - cr.left) / 2;
      if (dir == 0) {
        d.top = r.top - (cr.bottom - cr.top);
      }
      else {
        d.top = r.bottom;
      }
    }

    if (dir == 1 || dir == 3) {
      d.top = (r.top + r.bottom) / 2 - (cr.bottom - cr.top) / 2;
      d.bottom = d.top + (cr.bottom - cr.top);
      if (dir == 1) {
        d.left = r.right;
      }
      else {
        d.left = r.left - (cr.right - cr.left);
      }
    }

    d.right = d.left + (cr.right - cr.left);
    d.bottom = d.top + (cr.bottom - cr.top);

    DrawBorderedRectangle(d.left, d.top, (d.right - d.left), (d.bottom - d.top), D3DCOLOR_ARGB(128, 102, 52, 35), D3DCOLOR_ARGB(255, 255, 255, 255));
    g_font->DrawTextW(0, caption.c_str(), caption.size(), &d, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 255, 255));
  }

  RestoreD3D9RenderState();
  LeaveCriticalSection(&g_critsect_rectlist);
}