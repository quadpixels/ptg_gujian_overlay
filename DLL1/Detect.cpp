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

  int group_id; // Only 1 instance may appear in one. 0 = ignore
  int scene_id;

  MatchTemplate() { 
    scale = 0.5f;  // Use 0.5x resolution by default
    direction = 3;
    group_id = 0;
    scene_id = 0;
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
float g_ui_scale_factor = 1.0f, g_ui_scale_prev = 1.0f;

bool g_use_canny = false;
bool g_canny_set_scale_1 = false;
float g_canny_thresh1 = 100.0f, g_canny_thresh2 = 200.0f;
cv::Size g_boxfilter_size = cv::Size(4, 4);
float g_ccoeff_thresh = 0.7f;
int g_last_scene_id = -999;

void DetermineUIScaleFactor(const int w, const int h) {
  if (w == 1280 && h == 1024) g_ui_scale_factor = 1.0f;
  else {
    g_ui_scale_factor = 1.0f * h / 800;
    if (g_ui_scale_factor < 0.8f) g_ui_scale_factor = 0.8f;
  }
  if (g_ui_scale_factor != g_ui_scale_prev) {
    printf("[DetermineUIScaleFactor] %d x %d, scale_factor=%g\n", w, h, g_ui_scale_factor);
    g_ui_scale_prev = g_ui_scale_factor;
  }
}

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
  cv::Mat tmp_orig = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  raw_data_mat.release();

  std::unordered_map<float, cv::Mat*> resized;

  cv::Mat result;
  cv::TemplateMatchModes mode = cv::TM_CCOEFF_NORMED;

  std::unordered_set<int> groups;
  int curr_scene = 0; // Once scene is determined, skip all objects not belonging to that scene

  std::vector<int> template_idxes; //
  {
    std::unordered_map<int, std::list<int> > sid2idx;
    
    for (int i = 0; i < int(g_templates.size()); i++) {
      sid2idx[g_templates[i].scene_id].push_back(i);
    }

    if (false) { // put last scene's objects to the front?
      for (int i : sid2idx[g_last_scene_id]) {
        template_idxes.push_back(i);
      }
      sid2idx.erase(g_last_scene_id);
    }

    while (true) {
      bool added = false;
      for (std::unordered_map<int, std::list<int> >::iterator itr = sid2idx.begin(); itr != sid2idx.end(); itr++) {
        if (itr->second.empty() == false) {
          template_idxes.push_back(itr->second.front());
          itr->second.pop_front();
          added = true;
        }
      }
      if (!added) break;
    }
  }

  for (int x = 0; x < int(g_templates.size()); x++) {
    int i = template_idxes[x];
    const cv::Mat& m = *(g_templates[i].mat);
    const int group_id = g_templates[i].group_id;
    const int scene_id = g_templates[i].scene_id;

    if ((group_id != 0) && (groups.find(group_id) != groups.end())) continue;
    if (curr_scene != 0 && scene_id != curr_scene) continue;

    const float scale = g_templates[i].scale;

    cv::Mat* r = nullptr;
    if (resized.find(scale) == resized.end()) {

      cv::Mat tmp = tmp_orig.clone();

      if (g_ui_scale_factor != 1.0f) {
        cv::Mat origsize;
        cv::resize(tmp, origsize, cv::Size(0, 0), 1.0f / g_ui_scale_factor, 1.0f / g_ui_scale_factor, cv::INTER_NEAREST);
        tmp.release();
        origsize.assignTo(tmp);

        cv::imwrite("c:\\temp\\tmp.png", tmp);
      }

      if (g_use_canny) {
        cv::Mat tmp1;
        cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
        cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
        //tmp = tmp1;
      }

      r = new cv::Mat();
      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

      resized[scale] = r;
      tmp.release();
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
        if (metric > g_ccoeff_thresh) {
          found = true;
        }
      }
      break;
    }

    // Divide by scale gives coordinate at 100% UI scale
    // Multiplication by g_ui_scale_factor gives coordinates on rendered frame
    wprintf(L"   [DoDetect] %ls [Group %d] scale=%g, r=(%dx%d), Min Loc:(%d,%d)->(%d,%d); min&max val:(%g,%g) found=%d\n",
      g_templates[i].caption.c_str(), 
      g_templates[i].group_id,
      g_templates[i].scale,
      r->cols, r->rows,
      ans.x, ans.y,
      int(ans.x / scale * g_ui_scale_factor), int(ans.y / scale * g_ui_scale_factor),
      min_val, max_val,
      found);

    if (found == true) {
      RECT r_scaled, r_orig;
      r_orig.left = ans.x / scale * g_ui_scale_factor;
      r_orig.top = ans.y / scale * g_ui_scale_factor;
      r_orig.right = r_orig.left + (m.cols / scale * g_ui_scale_factor);
      r_orig.bottom = r_orig.top + (m.rows / scale * g_ui_scale_factor);

      r_scaled.left = ans.x;
      r_scaled.top = ans.y;
      r_scaled.right = r_scaled.left + m.cols;
      r_scaled.bottom = r_scaled.top + m.rows;

      struct MatchResult mr(i, r_scaled, r_orig);
      next_rect.push_back(mr);

      if (group_id != 0) groups.insert(group_id);
      curr_scene = scene_id;
    }
  }
  g_last_scene_id = curr_scene;

  tmp_orig.release();

  printf("<<< [DoDetect] |resized|=%lu\n", resized.size());

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

      if (g_ui_scale_factor != 1.0f) {
        cv::Mat origsize;
        cv::resize(tmp, origsize, cv::Size(0, 0), 1.0f / g_ui_scale_factor, 1.0f / g_ui_scale_factor);
        tmp = origsize;
      }

      if (g_use_canny) {
        cv::Mat tmp1(tmp.rows, tmp.cols, tmp.type());
        cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
        cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
        //tmp1.copyTo(*r);
        //tmp1.release();
      }

      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

      resized[scale] = r;
    }
    r = resized[scale];

    const int resized_w = mr.rect_scaled.right - mr.rect_scaled.left;
    const int resized_h = mr.rect_scaled.bottom - mr.rect_scaled.top;

    int ty = (g_use_canny) ? CV_8U : tmp.type();
    cv::Mat cropped(resized_h, resized_w, ty);

    for (int x = mr.rect_scaled.left, x0 = 0; x < mr.rect_scaled.right; x++, x0++) {
      for (int y = mr.rect_scaled.top, y0 = 0; y < mr.rect_scaled.bottom; y++, y0++) {
        //cropped.row(y0).col(x0) = r->row(y).col(x);
        if (g_use_canny == false)
          cropped.at<cv::Vec3b>(y0, x0) = r->at<cv::Vec3b>(y, x);
        else
          cropped.at<char>(y0, x0) = r->at<char>(y0, x0);
      }

      // DBG
      cv::imwrite("C:\\temp\\cropped.png", cropped);
    }

    cv::Mat result;
    cv::matchTemplate(cropped, *(mt.mat), result, cv::TM_CCOEFF_NORMED);

    cv::Point min_loc, max_loc;
    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc, cv::Mat());
    double metric = max_val;
    printf("[%d], metric=%g\n", idx, metric);
    if (metric < g_ccoeff_thresh) {
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
        float scale = 1.0f;

        std::getline(f, line);

        if (line[0] == '#') continue; // Commented out

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

          if (sp.size() >= 3 && sp[2] != "NA") {
            if (g_use_canny == true && g_canny_set_scale_1 == true)  {
              t.scale = 1.0f;
            } else {
              scale = std::stof(sp[2]);
              if (scale < 0 || scale > 1) scale = 0.5f;
              t.scale = scale;
            }
          }

          if (sp.size() >= 4 && sp[3] != "NA") {
            t.direction = std::stoi(sp[3]);
          }

          if (sp.size() >= 5 && sp[4] != "NA") {
            t.group_id = std::stoi(sp[4]);
          }

          if (sp.size() >= 6 && sp[5] != "NA") {
            t.scene_id = std::stoi(sp[5]);
          }

          cv::Mat tmp = cv::imread(file_name.c_str(), cv::IMREAD_COLOR);
          
          if (g_use_canny) {
            cv::Mat tmp1;
            cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
            cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
          }

          t.mat = new cv::Mat();

          cv::resize(tmp, *(t.mat), cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

          cv::Mat tmp1(t.mat->rows, t.mat->cols, t.mat->type());

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