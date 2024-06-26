#pragma once

#include <map>
#include <wchar.h>
#include <vector>
#include <Windows.h>
#include <set>
#include <string>
#include <sstream>
#include <fstream>
#include <d3d9.h>

extern long long MillisecondsNow();

struct RectCmp {
  bool operator()(const RECT& a, const RECT& b) const {
    if (a.top < b.top) return true;
    else if (a.top == b.top) {
      if (a.bottom < b.bottom) return true;
      else if (a.bottom == b.bottom) {
        if (a.left < b.left) return true;
        else if (a.left == b.left) {
          return a.right < b.right;
        }
        else return false;
      }
      else return false;
    }
    else return false;
  }
};

struct CustomVertex {
  float x, y, z;
  float rhw;
  DWORD color;
};

// 一句话
class A3Info { // Info contained in A3 files
public:
  std::wstring target_text;
  std::vector<std::wstring> target_text_split;
  std::vector<std::pair<std::wstring, std::vector<int> > > alignment;
  static std::wstring GetKey(const std::wstring& text);
  static std::wstring Unescape(const std::wstring& x);
};

class ClosedCaption {
  public:

    bool dialog_box_gone_when_fadingout;

    enum CaptionPlaybackState {
      CAPTION_NOT_PLAYING,
      CAPTION_PLAYING_NO_TIMELINE,
      CAPTION_PLAYING,
      CAPTION_PLAY_ENDED
    };

    // This flag is activated when the edge detector thinks the dialog box has disappeared when 
    // caption is in PLAYING state
    bool is_caption_playing_maybe_gone;

    ClosedCaption();
    static int PADDING_LEFT, PADDING_TOP, MOUSE_Y_DELTA;
    static int FADE_IN_MILLIS, FADE_OUT_MILLIS;

    int curr_story_idx;
    int font_size;
    bool is_enabled;

    // Index: content -> ID
    void LoadIndex(std::string filename) {
      std::ifstream f(filename.c_str());
      if (f.good()) {
        while (f.good()) {
          int id; std::string content, who;
          // 特殊情况：“风三水 风一淼” 中间有个空格
          std::string line;
          std::getline(f, line);
          std::istringstream ls(line);
          std::vector<std::string> sp;
          while (ls.good()) {
            std::string x;
            ls >> x;
            sp.push_back(x);
          }
          if (sp.size() >= 3) {
            id = atoi(sp[0].c_str());
            content = sp.back();
            for (int i = 1; i < int(sp.size()) - 1; i++) {
              if (who != "") who += " ";
              who = who + sp[i];
            }
          }
          if (who.size() > 0) {
            index[std::make_pair(who, content)].insert(id);
            //printf("(%s,%s) -> %d\n", who.c_str(), content.c_str(), id);
          }
        }
        printf("Loaded index %s, |index|=%lu\n", filename.c_str(), index.size());
        f.close();
      }
    }

    void LoadSeqDlgIndexFromMemory(void* ptr, int len);

    // CSV分隔，[ID, Char_CHS, CHS, Char_ENG, ENG_Rev]
    void LoadEngChsParallelText(const char* fn, const char* alignment_fn_e2c, const char* alignment_fn_c2e);

    void LoadEngChsParallelTextFromMemory(const char* ptr, int len);

    void LoadProperNamesList(const char* fn);

    void LoadProperNamesListFromMemory(const char* ptr, int len);

    void LoadA3AlignmentData(const char* fn);

    void OnFuncTalk(const char* who, const char* content);

    // Returns status code
    int SetAlignmentFileByAudioID(int id);

    // Sets speaker and content by ID
    void test_SetAudioIDWithParallelText(int id, bool x);

    int GuessNextAudioID(int id);

    std::wstring GetCurrLine();

    std::wstring curr_orig_text;
    std::wstring GetCurrOrigText() { return curr_orig_text; }
    void SetCurrOrigText(std::wstring x) { curr_orig_text = x; }

    std::wstring GetFullLine(bool ignore_speaker = false);

    // Line: "aaaaaaaaaaaaaaaaaaaaaa"
    //
    // Visual Line: "aaaaaaaaaaa
    //               aaaaaa     "
    //                         ^
    //                         |------ line width limit in px
    //
    std::vector<std::wstring> GetVisualLines(bool is_full);

    void ComputeBoundingBoxPerChar();

    void Draw();

    void Update();

    // For showing highlighted words
    // (idx, length)
    std::pair<int, int> highlighted_range;
    int last_highlight_idx;
    std::map<std::pair<float, float>, std::wstring> millis2word;
    std::set<std::pair<int, int> > proper_noun_ranges;

    // For showing alignment
    std::map<std::pair<int, int>, int> range_to_align_splits;
    
    A3Info* curr_a3info;
    int curr_a3_layout_linecount;
    void SetCurrA3Info(A3Info* curr_a3info);
    int do_drawA3Info(RECT* rect, bool is_render);

    std::pair<int, int> highlighted_range_align;
    int curr_a3_highlight_idx;

    void LoadDummy(const wchar_t* msg);

    void UpdateMousePos(int mx, int my);

    void OnMouseLeftDown(int mx, int my);

    void OnMouseLeftUp(int mx, int my);

    void UpdateDrag(int mx, int my);

    // The following 2 functions are similar
    void FindKeywordsForHighlighting();
    void MarkChsEngTranslationAlignments(A3Info* info);

    void do_FindKeywordsForHighlighting(std::wstring work);

    bool FindA3Alignment(const std::wstring& orig_text);

    void Clear() {
      proper_noun_ranges.clear();
      millis2word.clear();
      range_to_align_splits.clear();
    }

    void Hover();

    void ToggleIsDebug() { is_debug = !is_debug; }

    void ToggleIsEnabled() { is_enabled = !is_enabled; }

    bool IsDebug() { return is_debug; }

    bool IsVisible() {
      if (is_hovered) return true;
      else return (opacity > 0.0f);
    }

    bool IsMouseHover(int mx, int my) {
      if (mx >= x && mx <= x + GetVisibleWidth() &&
        my >= y && my <= y + GetVisibleHeight()) {
        return true;
      }
      else return false;
    }

    bool IsDragging() { return is_dragging; }

    void AutoPosition(int win_w, int win_h);

    void FadeIn();

    void FadeOut();

    float GetOpacity() { return opacity; }

  private:

    std::map<std::pair<std::string, std::string>, std::set<int> > index; // (who, content) -> Index

    // (English Text, IDX) -> (Original Text)
    std::map<std::pair<std::string, int>, std::string> eng2chs_who;
    std::map<std::pair<std::string, int>, std::string> eng2chs_content;
    std::map<std::pair<std::wstring, int>, std::wstring> eng2chs_content_w;
    std::map<std::pair<std::wstring, int>, std::wstring> chs2eng_content_w; // For debugging only

    // 高亮显示专用名词
    std::map<std::wstring, std::wstring> proper_names;

    // 翻译对齐
    std::map<std::wstring, A3Info*> tran_alignment_c2e, tran_alignment_e2c;

    std::wstring sentence, speaker;
    int line_width;
    int visible_height;

    float opacity, opacity_end, opacity_start;
    long long fade_end_millis, fade_duration;
    CaptionPlaybackState caption_state;
    bool is_hovered, is_dragging, is_debug;
    long long maybe_gone_suppress_until; // Suppresses "maybe-gone" detection until this time
    bool IsMaybeGoneSuppressed() { return MillisecondsNow() < maybe_gone_suppress_until; }
    void SuppressMaybeGone(int millis) {
      if (millis < 0) maybe_gone_suppress_until = 0;
      else maybe_gone_suppress_until = MillisecondsNow() + millis;
    }

    std::set<int> all_story_ids;

    long long start_millis;
    std::map<RECT, int, RectCmp> bb2char;
    int x, y;
    int mouse_x_start_drag, mouse_y_start_drag;
    int x_start_drag, y_start_drag;

    int GetVisibleWidth() { return this->line_width; }
    int GetVisibleHeight() { return this->visible_height; }

    RECT FindBbByCharIdx(int idx) {
      RECT ret = { 0 };
      std::map<RECT, int, RectCmp>::iterator itr = bb2char.begin();
      while (itr != bb2char.end()) {
        if (itr->second == idx) {
          ret = itr->first; break;
        }
        itr++;
      }
      ret.left += this->x;
      ret.right += this->x;
      ret.bottom += this->y;
      ret.top += this->y;
      return ret;
    }

    int FindTextIdxByMousePos(int x, int y, RECT* p_bb);

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

    // To be called from within Draw()
    void do_DrawHighlightedProperNouns(RECT rect_header, RECT rect, const int curr_text_offset, const std::wstring line, D3DCOLOR color);

    void do_DrawBorder(int x, int y, int w, int h);
  };


  class MyMessageBox {
  public:
    std::vector<std::wstring> lines;
    std::wstring message;
    int x, y, w, h;
    char gravity; // 7 0 1
                  // 6   2
                  // 5 4 3
    char text_align; // 0: Centered, -1: Left-Align, 1: Right-align
    static void StaticInit();

    MyMessageBox() : gravity(0), text_align(-1) { }
    ~MyMessageBox() { }

    void SetText(const std::wstring& msg, int width_limit);
    void CalculateDimension();
    void Draw();
    void Draw(D3DCOLOR bkcolor, D3DCOLOR bordercolor, D3DCOLOR fontcolor);
    void Hide() { w = h = 0; x = -999; y = -999; }
  };

  class VideoSubtitles {
  public:
    std::vector<std::wstring> subtitles;
    std::vector<int> millisecs;
    long start_millis;
    int curr_idx;
    enum State {
      SUBTITLES_NOT_STARTED,
      SUBTITLES_PLAYING,
      SUBTITLES_ENDED,
    };
    State state;
    void Init(std::vector<std::wstring> _subtitles, std::vector<int> _millisecs) {
      subtitles = _subtitles;
      millisecs = _millisecs;
      curr_idx = -999;
      state = SUBTITLES_NOT_STARTED;
      messagebox = nullptr;
    }
    VideoSubtitles() { }
    MyMessageBox* messagebox;
    std::wstring GetCurrString();
    void Draw();
    void Update();
    void Start();
    void Stop();
    void do_SetIdx(int idx);
    void LoadFromMemory(const char*, int);
  public:
    float GetCurrOpacity();
  };
