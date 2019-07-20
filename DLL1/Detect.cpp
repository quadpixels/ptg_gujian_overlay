#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <d3d9.h> // for RECT
#include <synchapi.h>

#include "ClosedCaption.h"

extern ClosedCaption g_ClosedCaption;

// 屏幕内容检测用
cv::Mat g_surface_mat;
cv::Mat g_template1;
const float g_scale = 0.5f; // multiply by this number before doing template matching
HANDLE g_detector_thd_handle;
DWORD  g_detector_thd_id;
volatile bool g_detector_done = false;
volatile bool g_detector_sleeping = true;
DWORD WINAPI MyThreadFunction(LPVOID lpParam);
void DoDetect(unsigned char* chr, const int len);

unsigned char* g_img_data;
int g_img_len;

void WakeDetectorThread(unsigned char* ch, const int len) {
  g_img_data = ch;
  g_img_len = len;

  MemoryBarrier();

  g_detector_sleeping = false;
}

DWORD WINAPI MyThreadFunction(LPVOID lpParam) {
  
  printf("[Thd %d] MyThreadFunction\n", int(GetCurrentThreadId()));

  while (g_detector_done == false) {
    while (g_detector_sleeping) {
      Sleep(50);
    }

    DoDetect(g_img_data, g_img_len);

    g_detector_sleeping = true;
  }

  return S_OK;
}

// About half second using debug library
void DoDetect(unsigned char* chr, const int len) {
  g_ClosedCaption.ClearHighlightRect();
  printf(">>> [DoDetect]\n");
  g_surface_mat.release();
  //g_surface_mat = cv::Mat(w, h, CV_8UC4, ch).t();

  cv::Mat raw_data_mat(1, len, CV_8UC1, chr);
  cv::Mat tmp = cv::imdecode(raw_data_mat, cv::IMREAD_COLOR);
  cv::resize(tmp, g_surface_mat, cv::Size(0, 0), g_scale, g_scale, cv::INTER_LINEAR);

  // DBG
  if (false) {
    cv::imwrite("C:\\temp\\imwrite.png", g_surface_mat);
  }

  cv::Mat result;
  cv::TemplateMatchModes mode = cv::TM_CCOEFF_NORMED;
  cv::matchTemplate(g_surface_mat, g_template1, result, mode);
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

  printf("<<< [DoDetect] Min Loc: (%d,%d); min&max val:(%g,%g) found=%d\n",
    int(ans.x / g_scale), int(ans.y / g_scale),
    min_val, max_val,
    found);

  if (found == true) {
    RECT r;
    r.left = ans.x / g_scale;
    r.top = ans.y / g_scale;
    r.right = r.left + g_template1.cols / g_scale;
    r.bottom = r.top + g_template1.rows / g_scale;
    g_ClosedCaption.AppendHighlightRect(r);
  }
}

void LoadImagesAndInitDetectThread() {
  cv::Mat tmp = cv::imread("C:\\Users\\nitroglycerine\\Downloads\\Gujian Resources\\new_game.png", cv::IMREAD_COLOR);
  cv::resize(tmp, g_template1, cv::Size(0, 0), g_scale, g_scale, cv::INTER_LINEAR);

  g_detector_thd_handle = CreateThread(
    nullptr,
    0,
    MyThreadFunction,
    nullptr,
    0,
    &g_detector_thd_id
  );

}