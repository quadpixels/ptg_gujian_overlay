#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include <d3d9.h> // for RECT
#include <d3dx9.h> // for FONT
#include <synchapi.h>
#include <unordered_map>
#include <unordered_set>
#include <list>

#include "ClosedCaption.h"
#include "config.h"
#include "miniz.h"

bool DEBUG_DIALOG_BOX = true;

extern ClosedCaption g_ClosedCaption;
extern void DrawBorderedRectangle(int x, int y, int w, int h, D3DCOLOR bkcolor, D3DCOLOR bordercolor);
extern CRITICAL_SECTION g_critsect_rectlist, g_calc_dimension;
extern CRITICAL_SECTION g_pnl_workq_critsect, g_pnl_mask_critsect;
extern LPD3DXFONT g_font;
extern IDirect3DSurface9* g_pSurfaceCopy;
extern LPD3DXBUFFER g_pSurfaceBuf;
extern PtgOverlayConfig g_config;
extern bool g_f11_flag;
extern int FONT_SIZE;

extern void BackupD3D9RenderState();
extern void RestoreD3D9RenderState();
extern void InitFont(LPD3DXFONT* pfont, int size); // in dll1.cpp
extern MyMessageBox* g_dbg_messagebox;
extern mz_zip_archive g_archive_match_templates;

extern HANDLE g_pnl_highlight_thd_handle;
extern DWORD  g_pnl_highlight_thd_id;
extern DWORD WINAPI PnlHighlightThdStart(LPVOID lpParam);

// TODO: Detect whether dialog box exists
// TODO: greyscale ??

// Gather metric statistics for offline study
struct MyHistogram {
  std::list<float> measurements;
  std::string name;
  int dump_offset;
  const int SAVE_INTERVAL = 100;
  MyHistogram(std::string _name) {
    dump_offset = 0;
    name = _name;
  }
  void AddMeasurement(float x) {
    measurements.push_back(x);
    if ((measurements.size() % SAVE_INTERVAL) == (SAVE_INTERVAL - 1)) {
      DumpToFile();
    }
  }
  void DumpToFile() {
    char tmp[255];
    sprintf_s(tmp, "C:\\temp\\%s.txt", name.c_str());
    FILE* f;
    errno_t e = fopen_s(&f, tmp, "a");
    if (e != 0) return;
    int idx = 0;
    for (float m : measurements) {
      if (idx >= dump_offset) {
        fprintf(f, "%g\n", m);
      }
      idx++;
    }
    dump_offset = int(measurements.size());
    fclose(f);
  }
  ~MyHistogram() {
    DumpToFile();
  }
};

// 每个Template 可以有自己的 scale
struct MatchTemplate {
  // 静态属性
  cv::Mat* mat, *mask;
  float scale;
  std::wstring caption;
  int direction; // 0123 = URDL

  int group_id; // Only 1 instance may appear in one. 0 = ignore
  int scene_id;

  float sqdiff_thresh;

  // 动态属性
  int last_frame_match_count;

  MyHistogram* histogram;

  MatchTemplate() {
    histogram = nullptr;
    mask = nullptr;
    scale = 0.5f;  // Use 0.5x resolution by default
    direction = 3;
    group_id = 0;
    scene_id = 0;

    last_frame_match_count = 0;
    sqdiff_thresh = 100.0f;
  }

  bool ShouldUseCanny() const { 
    return false;
    //return (mask != nullptr); 
  }

  bool UsesMask() const {
    return (mask != nullptr);
  }
};

// 屏幕内容检测用
cv::Mat g_surface_mat;
std::vector<MatchTemplate*> g_templates;
std::unordered_map < MatchTemplate*, std::pair<int, int> > g_match_cache;
const int g_cache_probe_radius = 5; // Scaled px size
HANDLE g_detector_thd_handle;
DWORD  g_detector_thd_id;
volatile bool g_detector_done = false;
volatile bool g_detector_sleeping = true;
DWORD WINAPI MyThreadFunction(LPVOID lpParam);
void DoDetect(unsigned char* chr, const int len);
void DoCheckMatchesDisappear(unsigned char* chr, const int len);
float g_ui_scale_factor = 1.0f, g_ui_scale_prev = 1.0f;
int g_last_w = 0, g_last_h = 0;

const bool IMPATIENT = true;
const int CACHE_SIZE_LIMIT = 1;

bool g_use_canny = false;
bool g_canny_set_scale_1 = false;
float g_canny_thresh1 = 100.0f, g_canny_thresh2 = 200.0f;
cv::Size g_boxfilter_size = cv::Size(4, 4);
float g_ccoeff_thresh = 0.7f;
int g_last_scene_id = -999;

bool g_dialog_box_gone = false;
std::map<std::pair<int, int>, RECT > DIALOG_BOX_COORDS =
{
  { { 1024, 720 }, { 173, 529, 812, 643 } },
  { { 1024, 768 }, { 173, 554, 812, 669 } },
  { { 1280, 720 }, { 258, 550, 962, 681 } },
  { { 1280, 800 }, { 216, 611, 1007, 757 } },
  { { 1280, 1024}, { 216, 723, 1007, 869 } },
  { { 1366, 768 }, { 274, 587, 1060, 727 } },
  { { 1440, 900 }, { 243, 688, 1127, 851 } },
  { { 1600, 900 }, { 323, 688, 1209, 851 } },
  { { 1680, 1050}, { 284, 802, 1350, 993} },
  { { 1920, 1080}, { 388, 825, 1463, 1022} },
};
RECT g_curr_dialogbox_rect;

void DetermineUIScaleFactor(const int w, const int h) {
  // Determine UI Scale Factor
  if (w == 1280 && h == 1024) g_ui_scale_factor = 1.0f;
  else if (w == 1024) g_ui_scale_factor = 0.8f;
  else {
    g_ui_scale_factor = 1.0f * h / 800;
    if (g_ui_scale_factor < 0.8f) g_ui_scale_factor = 0.8f;
  }

  if (g_ui_scale_factor != g_ui_scale_prev) {
    printf("[DetermineUIScaleFactor] %d x %d, scale_factor=%g\n", w, h, g_ui_scale_factor);
    g_ui_scale_prev = g_ui_scale_factor;
    InitFont(&g_font, FONT_SIZE * g_ui_scale_factor);
  }

  // Determine location of dialog box
  std::pair<int, int> key = std::make_pair(w, h);
  if (DIALOG_BOX_COORDS.find(key) != DIALOG_BOX_COORDS.end()) {
    g_curr_dialogbox_rect = DIALOG_BOX_COORDS[key];
  }
  else {
    g_curr_dialogbox_rect = DIALOG_BOX_COORDS.rbegin()->second;
  }

  // When we have dialog box RECT, apply it to AutoPosition
  if (g_last_w != w || g_last_h != h) {
    g_match_cache.clear();
    g_ClosedCaption.AutoPosition(w, h);
  }

  g_last_w = w; g_last_h = h;
}

bool RectEqual(const RECT& r1, const RECT& r2) {
  if (r1.bottom == r2.bottom &&
    r1.left == r2.left &&
    r1.right == r2.right &&
    r1.top == r2.top) return true;
  else return false;
}

struct MatchResult {
  int idx;
  RECT rect_scaled;
  RECT rect_orig;
  bool operator==(const struct MatchResult& other) const {
    return (idx == other.idx 
      && RectEqual(rect_scaled, other.rect_scaled)
      && RectEqual(rect_orig, other.rect_orig));
  }
  MatchResult(int _idx, RECT _scaled, RECT _orig) {
    idx = _idx;
    rect_scaled = _scaled;
    rect_orig = _orig;
  }
};
std::vector<MatchResult> highlight_rects;

void ClearHighlightRects() {
  EnterCriticalSection(&g_critsect_rectlist);
  highlight_rects.clear();
  g_match_cache.clear();
  LeaveCriticalSection(&g_critsect_rectlist);
}

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

// TODO: Support different pixel formats
cv::Mat MyCrop(const cv::Mat in, RECT range, bool is_single_channel) {
  const int W = range.right - range.left, H = range.bottom - range.top;
  cv::Mat ret(H, W, in.type());
  for (int y0 = range.top, y1 = 0; y0 < range.bottom; y0++, y1++) {
    for (int x0 = range.left, x1 = 0; x0 < range.right; x0++, x1++) {
      if (is_single_channel) ret.at<unsigned char>(y1, x1) = in.at<unsigned char>(y0, x0);
      else ret.at<cv::Vec3b>(y1, x1) = in.at<cv::Vec3b>(y0, x0);
    }
  }
  return ret;
}

bool IsDialogBoxMaybeGone() {
  return g_dialog_box_gone;
}

// About half second using debug library
void DoDetect(unsigned char* chr, const int len) {
  const int N = int(g_templates.size()); // This can help avoid data race!
  std::vector<int> this_frame_match_count;
  std::wstring dbg_string = L"";
  const bool VERBOSE = true;

  std::vector<struct MatchResult> next_rect;
  //printf(">>> [DoDetect]\n");
  //g_surface_mat = cv::Mat(w, h, CV_8UC4, ch).t();

  cv::Mat raw_data_mat(1, len, CV_8UC1, chr);
  cv::Mat tmp_orig = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  raw_data_mat.release();

  // Update dialog box presence
  {
    // Generate canny and return
    cv::Mat canny_orig;
    cv::Canny(tmp_orig, canny_orig, g_canny_thresh1, g_canny_thresh2);
    

    int sc_outer = 0, sc_inner = 0;
    int R = 5; // probe radius
    const int x0 = g_curr_dialogbox_rect.left, y0 = g_curr_dialogbox_rect.top,
      x1 = g_curr_dialogbox_rect.right, y1 = g_curr_dialogbox_rect.bottom;

    // To be safe with testbench
    if (canny_orig.rows > y1 + 2 + R && canny_orig.cols > x1 + 2 + R) {
      for (int x = x0; x <= x1; x++) {
        for (int y = y0 - 2; y >= y0 - 2 - R; y--) {
          if (canny_orig.at<char>(y, x) != 0) {
            sc_outer++;
          }
        }
        for (int y = y0 + 2; y <= y0 + 2 + R; y++) {
          if (canny_orig.at<char>(y, x) != 0) {
            sc_inner++;
          }
        }

        for (int y = y1 - 2; y >= y1 - 2 - R; y--) {
          if (canny_orig.at<char>(y, x) != 0) {
            sc_inner++;
          }
        }
        for (int y = y1 + 2; y <= y1 + 2 + R; y++) {
          if (canny_orig.at<char>(y, x) != 0) {
            sc_outer++;
          }
        }
      }
    }

    printf("sc_outer=%d sc_inner=%d\n", sc_outer, sc_inner);

    if (sc_outer > 1.25 * sc_inner) {
      g_dialog_box_gone = false;
    }
    else {
      g_dialog_box_gone = true;
    }
  }

  std::map<std::pair<float, bool>, cv::Mat*> resized;
  cv::Mat result;
  std::unordered_set<int> groups;
  int curr_scene = 0; // Once scene is determined, skip all objects not belonging to that scene
  std::vector<int> template_idxes; //
  {
    std::unordered_map<int, std::list<int> > sid2idx;
    
    for (int i = 0; i < N; i++) {
      sid2idx[g_templates[i]->scene_id].push_back(i);
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

  this_frame_match_count.resize(N);
  for (int i = 0; i < this_frame_match_count.size(); i++) this_frame_match_count[i] = 0;

  for (int x = 0; x < N; x++) {
    cv::TemplateMatchModes mode = cv::TM_CCOEFF_NORMED;
    
    int i = template_idxes[x];
    const cv::Mat& m = *(g_templates[i]->mat), *mask = g_templates[i]->mask;
    const int group_id = g_templates[i]->group_id;
    const int scene_id = g_templates[i]->scene_id;

    if ((group_id != 0) && (groups.find(group_id) != groups.end())) continue;
    if (curr_scene != 0 && scene_id != curr_scene) continue;

    const float scale = g_templates[i]->scale;
    const bool use_canny = g_templates[i]->ShouldUseCanny();
    const bool use_mask = g_templates[i]->UsesMask();

    if (use_mask) mode = cv::TM_SQDIFF;

    std::pair<float, bool> key = { scale, use_canny };

    cv::Mat* r = nullptr;
    if (resized.find(key) == resized.end()) {

      cv::Mat tmp = tmp_orig.clone();

      if (g_ui_scale_factor != 1.0f) {
        cv::Mat origsize;
        cv::resize(tmp, origsize, cv::Size(0, 0), 1.0f / g_ui_scale_factor, 1.0f / g_ui_scale_factor, cv::INTER_NEAREST);
        tmp.release();
        origsize.assignTo(tmp);
      }

      if (use_canny) {
        cv::Mat tmp1;
        cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
        cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
        //tmp = tmp1;
      }

      r = new cv::Mat();
      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

      resized[key] = r;
      tmp.release();
    }

    r = resized[key];

    // Cache or not cache?
    bool in_cache = (g_match_cache.find(g_templates[i]) != g_match_cache.end());
    cv::Point min_loc, max_loc;
    double min_val, max_val;

    if (in_cache) { // Partial match
      std::pair<int, int> p = g_match_cache[g_templates[i]];
      RECT c;
      c.left = p.first; c.top = p.second;
      c.right = c.left + g_templates[i]->mat->cols;
      c.bottom = c.top + g_templates[i]->mat->rows;
      cv::Mat roi = MyCrop(*r, c, use_canny);
      if (use_mask) cv::matchTemplate(roi, m, result, mode, *mask);
      else cv::matchTemplate(roi, m, result, mode);
      cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc, cv::Mat());
      min_loc.x += c.left; min_loc.y += c.top;
      max_loc.x += c.left; max_loc.y += c.top;
    } else { // Full match
      if (use_mask) {
        cv::matchTemplate(*r, m, result, mode, *mask);
      }
      else cv::matchTemplate(*r, m, result, mode);
      cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc, cv::Mat());
    }

    cv::Point ans;
    double metric;
    bool found = false;
    float thresh_mult = 1.0f;

    switch (mode) {
    case cv::TM_SQDIFF: case cv::TM_SQDIFF_NORMED:
      ans = min_loc;
      metric = min_val;
      
      if (g_templates[i]->last_frame_match_count > 0) {
        thresh_mult = 3.0f;
      }

      if (min_val < g_templates[i]->sqdiff_thresh * thresh_mult) {
        found = true;
      }
      break;
    default:
      ans = max_loc;
      metric = max_val;
      if (mode == cv::TM_CCOEFF_NORMED) {
        if (metric > g_ccoeff_thresh) {
          found = true;
        }
      }
      else if (mode == cv::TM_CCORR_NORMED) {
        if (metric > 0.95) {
          found = true;
        }
      }
      break;
    }

    // Record metric if histogram struct exists
    if (g_ClosedCaption.IsDebug()) {
      if (g_templates[i]->histogram != nullptr) {
        g_templates[i]->histogram->AddMeasurement(metric);
      }
    }

    // Divide by scale gives coordinate at 100% UI scale
    // Multiplication by g_ui_scale_factor gives coordinates on rendered frame
    if (VERBOSE) {
      wchar_t buf[233];
      std::wstring x = g_templates[i]->caption;
      if (x.size() > 14) {
        x = x.substr(0, 11) + L"...";
      }
      swprintf_s(
        buf,
        L"[%2d] %-14ls lfm=%d %1.1fx %d (%.3f,%.3f) %ls %ls\n",
        i,
        x.c_str(),
        g_templates[i]->last_frame_match_count,
        //g_templates[i]->group_id,
        g_templates[i]->scale,
        mode,
        //r->cols, r->rows,
        //ans.x, ans.y,
        //int(ans.x / scale * g_ui_scale_factor), int(ans.y / scale * g_ui_scale_factor),
        min_val, max_val,
        (in_cache ? L"$" : L"_"),
        (found ? L"Found" : L""));
      
      if (g_ClosedCaption.IsDebug()) {
        dbg_string += buf;
      }
    }
    else {
      /*
        if (in_cache) printf("C");
        else if (found) printf("+");
        else printf(".");
        fflush(stdout);
      */
    }

    if (found == true) {
      this_frame_match_count[i]++;

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

      if (IMPATIENT) { // 发现了就马上渲染，不要让用户等太久
        EnterCriticalSection(&g_critsect_rectlist);
        if (std::find(highlight_rects.begin(), highlight_rects.end(), mr) == highlight_rects.end()) {
          highlight_rects.push_back(mr);
        }
        LeaveCriticalSection(&g_critsect_rectlist);
      }

      g_match_cache[g_templates[i]] = std::make_pair(r_scaled.left, r_scaled.top);

      if (group_id != 0) groups.insert(group_id);
      curr_scene = scene_id;
    }
  }
  g_last_scene_id = curr_scene;

  tmp_orig.release();

  ///printf("<<< [DoDetect] |resized|=%lu\n", resized.size());

  for (std::pair<std::pair<float, bool>, cv::Mat*> p : resized) {
    p.second->release();
    delete p.second;
  }

  EnterCriticalSection(&g_critsect_rectlist);
  highlight_rects.clear();
  highlight_rects = next_rect;
  LeaveCriticalSection(&g_critsect_rectlist);

  g_dbg_messagebox->SetText(dbg_string, 800*g_ui_scale_factor);
  // CalcDimension should be in main thread!

  for (int i = 0; i < N; i++) {
    g_templates[i]->last_frame_match_count = this_frame_match_count[i];
  }
}

void DoCheckMatchesDisappear(unsigned char* chr, const int len) {
  cv::Mat raw_data_mat(1, len, CV_8UC1, chr);
  cv::Mat tmp = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  raw_data_mat.release();
  assert(tmp.type() == CV_8UC3);
  
  //                           Scale  Canny
  std::map<std::pair<float, bool>, cv::Mat*> resized;
  std::unordered_set<int> victim_idxes;

  int idx = 0;
  for (const MatchResult& mr : highlight_rects) {
    const MatchTemplate& mt = *(g_templates[mr.idx]);
    const float scale = mt.scale;
    const bool  use_canny = (mt.ShouldUseCanny());
    std::pair<float, bool> key = { scale, use_canny };

    // Obtain texture that matches the template's scale
    cv::Mat* r = nullptr;
    if (resized.find(key) == resized.end()) {
      r = new cv::Mat();

      if (g_ui_scale_factor != 1.0f) {
        cv::Mat origsize;
        cv::resize(tmp, origsize, cv::Size(0, 0), 1.0f / g_ui_scale_factor, 1.0f / g_ui_scale_factor);
        tmp = origsize;
      }

      if (use_canny) {
        cv::Mat tmp1(tmp.rows, tmp.cols, tmp.type());
        cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
        cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
        //tmp1.copyTo(*r);
        //tmp1.release();
      }

      cv::resize(tmp, *r, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);

      resized[key] = r;
    }
    r = resized[key];

    const int resized_w = mr.rect_scaled.right - mr.rect_scaled.left;
    const int resized_h = mr.rect_scaled.bottom - mr.rect_scaled.top;

    int ty = (use_canny) ? CV_8U : tmp.type();
    cv::Mat cropped(resized_h, resized_w, ty);

    for (int x = mr.rect_scaled.left, x0 = 0; x < mr.rect_scaled.right; x++, x0++) {
      for (int y = mr.rect_scaled.top, y0 = 0; y < mr.rect_scaled.bottom; y++, y0++) {
        //cropped.row(y0).col(x0) = r->row(y).col(x);
        if (use_canny == false)
          cropped.at<cv::Vec3b>(y0, x0) = r->at<cv::Vec3b>(y, x);
        else
          cropped.at<char>(y0, x0) = r->at<char>(y0, x0);
      }

      // DBG
      //cv::imwrite("C:\\temp\\cropped.png", cropped);
    }

    cv::Mat result;
    if (mt.mask) cv::matchTemplate(cropped, *(mt.mat), result, cv::TM_CCOEFF_NORMED, *(mt.mask));
    else cv::matchTemplate(cropped, *(mt.mat), result, cv::TM_CCOEFF_NORMED);

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

  for (std::pair<std::pair<float, bool>, cv::Mat*>p : resized) {
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

void LoadImagesFromFile() // Read Image List
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

      // Must use "No-BOM" mode
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
        MatchTemplate* t = new MatchTemplate();
        file_name = dir_name + "\\" + sp[0]; caption = sp[1];

        cv::Mat tmp = cv::imread(file_name.c_str(), cv::IMREAD_COLOR);
        // Get base name, mask name and check if mask exists
        {
          std::string base_name = file_name.substr(0, file_name.size() - 4);
          std::string ext_name = file_name.substr(file_name.size() - 3);
          std::string mask_fn = base_name + "_mask." + ext_name;
          FILE* f; fopen_s(&f, mask_fn.c_str(), "rb");
          if (f) {
            fclose(f);
            t->mask = new cv::Mat();
            *(t->mask) = cv::imread(mask_fn.c_str(), cv::IMREAD_COLOR);
            cv::Mat tmp;
            cv::resize(*(t->mask), tmp, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
            *(t->mask) = tmp;
          }
        }

        bool use_canny = t->ShouldUseCanny();

        if (sp.size() >= 3 && sp[2] != "NA") {
          if (use_canny == true && g_canny_set_scale_1 == true) {
            t->scale = 1.0f;
          }
          else {
            scale = std::stof(sp[2]);
            if (scale < 0 || scale > 1) scale = 0.5f;
            t->scale = scale;
          }
        }

        if (use_canny) {
          cv::Mat tmp1;
          cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
          cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
        }

        if (sp.size() >= 4 && sp[3] != "NA") {
          t->direction = std::stoi(sp[3]);
        }

        if (sp.size() >= 5 && sp[4] != "NA") {
          t->group_id = std::stoi(sp[4]);
        }

        if (sp.size() >= 6 && sp[5] != "NA") {
          t->scene_id = std::stoi(sp[5]);
        }

        if (sp.size() >= 7 && sp[6] != "NA") {
          t->sqdiff_thresh = std::stof(sp[6]);
        }

        t->mat = new cv::Mat();

        // Resize image and mask
        cv::resize(tmp, *(t->mat), cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
        if (t->mask != nullptr) {
          cv::Mat tmp2;
          cv::resize(*(t->mask), tmp2, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
          tmp2.copyTo(*(t->mask));
        }

        cv::Mat tmp1(t->mat->rows, t->mat->cols, t->mat->type());

        // MBS to WCS
        int len_caption = MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, NULL, NULL);
        wchar_t* wcaption = new wchar_t[len_caption];
        MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, wcaption, len_caption);
        t->caption = wcaption;
        delete wcaption;

        // assign histogram if in debug mode
        t->histogram = new MyHistogram(caption);

        printf("Loaded template [%s] = %dx%d, resized=%dx%d, has mask: %s\n",
          file_name.c_str(), tmp.cols, tmp.rows, t->mat->cols, t->mat->rows,
          (t->mask == nullptr ? "No" : "Yes"));
        g_templates.push_back(t);
      }
    }
  }
}

void LoadImagesFromResource() {
  size_t sz;
  const char* buf = (const char*)(mz_zip_reader_extract_file_to_heap(&g_archive_match_templates, "TemplateList.txt", &sz, 0));
  int idx = 0;
  while (idx < sz) {
    std::string file_name, caption;
    std::string line, x;
    float scale = 1.0f;

    // Get line
    while (idx < sz) {
      bool has_new_line = false;
      while (idx < sz && (buf[idx] == '\n' || buf[idx] == '\r')) {
        idx++;
        has_new_line = true;
      }

      if (has_new_line) break;
      
      if (idx < sz) {
        line.push_back(buf[idx]);
        idx++;
      }
    }

    printf("[LoadImagesFromResource] line=%s\n", line.c_str());

    // "No-BOM UTF-8" mode
    if (line.size() > 0 && line[0] == '#') continue; // Commented out
    // "BOM UTF-8" mode
    if ((line.size() > 3) && 
        ((line[0] & 0xff) == 0xef) && // For signed chars, 1's will be prepended when promoting to signed int!
        ((line[1] & 0xff) == 0xbb) && 
        ((line[2] & 0xff) == 0xbf) && (line[3] == 0x23)) continue;

    // Get line
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

    // Split line
    if (sp.size() >= 2) {
      MatchTemplate* t = new MatchTemplate();
      file_name = sp[0]; caption = sp[1];
      // No directories in zip file
      file_name = file_name.substr(file_name.rfind('\\') + 1);

      cv::Mat tmp;

      size_t sz_img = 0;
      unsigned char* buf_img = (unsigned char*)(mz_zip_reader_extract_file_to_heap(&g_archive_match_templates, file_name.c_str(), &sz_img, 0));
      if (sz_img > 0 && buf_img != nullptr) {
        cv::Mat raw_data_mat(1, sz_img, CV_8UC1, buf_img);
        tmp = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
      }

      // Get base name, mask name and check if mask exists
      {
        std::string base_name = file_name.substr(0, file_name.size() - 4);
        std::string ext_name = file_name.substr(file_name.size() - 3);
        std::string mask_fn = base_name + "_mask." + ext_name;
        size_t sz_mask = 0;
        unsigned char* buf_mask = (unsigned char*)(mz_zip_reader_extract_file_to_heap(&g_archive_match_templates, mask_fn.c_str(), &sz_mask, 0));
        if (sz_mask > 0 && buf_mask != nullptr) {
          t->mask = new cv::Mat();
          cv::Mat raw_data_mat(1, sz_mask, CV_8UC1, buf_mask);
          *(t->mask) = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
          cv::Mat tmp_mask;
          cv::resize(*(t->mask), tmp_mask, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
          *(t->mask) = tmp_mask;
        }
        delete buf_mask;
      }

      bool use_canny = t->ShouldUseCanny();

      if (sp.size() >= 3 && sp[2] != "NA") {
        if (use_canny == true && g_canny_set_scale_1 == true) {
          t->scale = 1.0f;
        }
        else {
          scale = std::stof(sp[2]);
          if (scale < 0 || scale > 1) scale = 0.5f;
          t->scale = scale;
        }
      }

      if (use_canny) {
        cv::Mat tmp1;
        cv::Canny(tmp, tmp1, g_canny_thresh1, g_canny_thresh2);
        cv::boxFilter(tmp1, tmp, -1, g_boxfilter_size);
      }

      if (sp.size() >= 4 && sp[3] != "NA") {
        t->direction = std::stoi(sp[3]);
      }

      if (sp.size() >= 5 && sp[4] != "NA") {
        t->group_id = std::stoi(sp[4]);
      }

      if (sp.size() >= 6 && sp[5] != "NA") {
        t->scene_id = std::stoi(sp[5]);
      }

      if (sp.size() >= 7 && sp[6] != "NA") {
        t->sqdiff_thresh = std::stof(sp[6]);
      }

      t->mat = new cv::Mat();

      // Resize image and mask
      cv::resize(tmp, *(t->mat), cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
      if (t->mask != nullptr) {
        cv::Mat tmp2;
        cv::resize(*(t->mask), tmp2, cv::Size(0, 0), scale, scale, cv::INTER_LINEAR);
        tmp2.copyTo(*(t->mask));
      }

      cv::Mat tmp1(t->mat->rows, t->mat->cols, t->mat->type());

      // MBS to WCS
      int len_caption = MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, NULL, NULL);
      wchar_t* wcaption = new wchar_t[len_caption];
      MultiByteToWideChar(CP_UTF8, 0, caption.c_str(), -1, wcaption, len_caption);
      t->caption = wcaption;
      delete wcaption;


      // assign histogram if in debug mode
      t->histogram = new MyHistogram(caption);

      printf("Loaded template [%s] = %dx%d, resized=%dx%d, has mask: %s\n",
        file_name.c_str(), tmp.cols, tmp.rows, t->mat->cols, t->mat->rows,
        (t->mask == nullptr ? "No" : "Yes"));
      g_templates.push_back(t);
    }
  }
}

void LoadImagesAndInitWorkerThreads() {

  InitializeCriticalSection(&g_critsect_rectlist);
  InitializeCriticalSection(&g_calc_dimension);
  InitializeCriticalSection(&g_pnl_workq_critsect);
  InitializeCriticalSection(&g_pnl_mask_critsect);

  g_detector_thd_handle = CreateThread(
    nullptr,
    0,
    MyThreadFunction,
    nullptr,
    0,
    &g_detector_thd_id
  );

  g_pnl_highlight_thd_handle = CreateThread(
    nullptr,
    0,
    PnlHighlightThdStart,
    &g_ClosedCaption,
    0,
    &g_pnl_highlight_thd_id
  );
}

void DrawHighlightRects() {
  EnterCriticalSection(&g_critsect_rectlist);
  BackupD3D9RenderState();

  for (const MatchResult& mr : highlight_rects) {
    const struct MatchTemplate& t = *(g_templates[mr.idx]);
    const std::wstring& caption = t.caption;
    D3DCOLOR border_color = D3DCOLOR_ARGB(16, 255, 255, 255);

    // Measure text width
    RECT cr; // CR = Caption Rect
    g_font->DrawTextW(NULL, caption.c_str(), caption.size(), &cr, DT_CALCRECT, D3DCOLOR_ARGB(255, 255, 255, 255));

    RECT r = mr.rect_orig;
    if (g_ClosedCaption.IsDebug()) {
      DrawBorderedRectangle(r.left, r.top, (r.right - r.left), (r.bottom - r.top), D3DCOLOR_ARGB(0, 0, 0, 0), D3DCOLOR_ARGB(255, 32, 255, 32));
    }

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

    DrawBorderedRectangle(d.left, d.top, (d.right - d.left), (d.bottom - d.top), D3DCOLOR_ARGB(128, 102, 52, 35), border_color);
    g_font->DrawTextW(0, caption.c_str(), caption.size(), &d, DT_NOCLIP, D3DCOLOR_ARGB(255, 255, 255, 255));
  }

  if (g_ClosedCaption.IsDebug()) {
    RECT x = g_curr_dialogbox_rect;
    if (IsDialogBoxMaybeGone() == false)
      DrawBorderedRectangle(x.left, x.top, (x.right - x.left), (x.bottom - x.top), D3DCOLOR_ARGB(128, 32, 255, 32), D3DCOLOR_ARGB(128, 32, 255, 32));
  }

  RestoreD3D9RenderState();
  LeaveCriticalSection(&g_critsect_rectlist);
}