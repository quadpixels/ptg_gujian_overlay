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

class ClosedCaption {
  public:
    ClosedCaption();
    static int PADDING_LEFT, PADDING_TOP, MOUSE_Y_DELTA;

    std::map<std::pair<std::string, std::string>, std::set<int> > index; // (who, content) -> Index

    // 用于英文文本 --> 中文文本的映射
    std::map<std::string, std::string> eng2chs_who;
    std::map<std::string, std::string> eng2chs_content;

    // 高亮显示的专用名词
    std::map<std::wstring, std::wstring> proper_names;

    std::wstring sentence, speaker;
    static std::string alignment_path;
    int line_width;
    int visible_height;

    static void SetAlignmentPath() {
      char user_directory[233];
      GetEnvironmentVariableA("USERPROFILE", user_directory, 233);
      alignment_path = std::string(user_directory) + std::string("\\Downloads\\GuJian Resources\\");
    }

    int curr_story_idx;
    int font_size;

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

    void LoadIndexFromMemory(void* ptr, int len) {
      char* p = (char*)ptr;
      int idx = 0, num_ety = 0;
      std::string line;
      while (idx <= len) {
        if (idx == len || p[idx] == '\r' || p[idx] == '\n') {
          if (line.size() > 0) {
            // getline ok
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
            }

            if (who.size() > 0) {
              index[std::make_pair(who, content)].insert(id);
              num_ety++;
            }
          }
        }
        else {
          if (idx < len) {
            line.push_back(p[idx]);
          }
          else {
            break;
          }
        }
        idx++;
      }
      //printf("[LoadIndexFromMemory] %d entries\n", num_ety);
    }

    // CSV分隔，[ID, Char_CHS, CHS, Char_ENG, ENG_Rev]
    void LoadEngChsParallelText(const char* fn) {
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
            eng2chs_who[char_eng] = char_chs;
            eng2chs_content[eng_rev] = chs;
            num_entries++;
            //printf("[%s]->[%s], [%s]->[%s]\n", char_eng.c_str(), char_chs.c_str(), eng_rev.c_str(), chs.c_str());
          }
        }
        f.close();
      }
      printf("%d entries in parallel text [%s] scanned\n", num_entries, fn);
    }

    void LoadProperNamesList(const char* fn);

    void OnFuncTalk(const char* who, const char* content) {

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
        if (eng2chs_who.find(who1) != eng2chs_who.end() &&
          eng2chs_content.find(content1) != eng2chs_content.end()) {
          who1 = eng2chs_who[who1];
          mapped = true;
          content1 = eng2chs_content[content1];
          printf("ENG --> CHS found!\n");
          printf("                  remapped([%s],[%s])\n", who1.c_str(), content1.c_str());
        }
      }

      int len = MultiByteToWideChar(CP_UTF8, 0, content1.c_str(), -1, NULL, NULL);
      int len_who = MultiByteToWideChar(CP_UTF8, 0, who1.c_str(), -1, NULL, NULL);

      wchar_t* wcontent = new wchar_t[len]; // Hopefully this will be enough!
      wchar_t* wwho = new wchar_t[len_who];
      MultiByteToWideChar(CP_UTF8, 0, content1.c_str(), -1, wcontent, len);
      MultiByteToWideChar(CP_UTF8, 0, who1.c_str(), -1, wwho, len_who);

      std::wstring w = wcontent, w1 = wwho;

      speaker = w1;

      delete wcontent;
      delete wwho;

      int ret = FindAlignmentFile(who1.c_str(), content1.c_str());
      if (ret == -999 || ret == -998) {
        millis2word.clear();
        millis2word[std::make_pair(0.0f, 0.0f)] = w;
        printf("[ClosedCaption] alignment file not found or is not okay. length=%lu\n", w.size());
      }
      else {
        printf("[ClosedCaption] alignment file found\n");
      }

      // 无论找没找到都查找关键字
      FindKeywordsForHighlighting();
    }

    // Returns status code
    int SetAlignmentFileByAudioID(int id);

    std::wstring GetCurrLine();

    std::wstring GetFullLine();

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

    // (idx, length)
    std::pair<int, int> highlighted_range;
    int last_highlight_idx;
    std::map<std::pair<float, float>, std::wstring> millis2word;
    std::set<std::pair<int, int> > proper_noun_ranges;
    std::vector<bool> proper_noun_mask;

    void LoadDummy();

    void UpdateMousePos(int mx, int my);

    void OnMouseLeftDown(int mx, int my);

    void OnMouseLeftUp(int mx, int my);

    void UpdateDrag(int mx, int my);

    void FindKeywordsForHighlighting();

    void Clear() {
      proper_noun_mask.clear();
      proper_noun_ranges.clear();
      millis2word.clear();
    }

    void ToggleVisibility() {
      visible = !visible;
    }

    bool IsVisible() {
      return visible;
    }

    bool IsMouseHover(int mx, int my) {
      if (mx >= x && mx <= x + GetVisibleWidth() &&
        my >= y && my <= y + GetVisibleHeight()) {
        return true;
      }
      else return false;
    }

    bool IsDragging() { return is_dragging; }


  private:
    bool visible;
    bool is_hovered, is_dragging;
    int last_mainstory;
    long long start_millis;
    std::map<RECT, int, RectCmp> bb2char;
    int x, y;
    int mouse_x_start_drag, mouse_y_start_drag;
    int x_start_drag, y_start_drag;

    int GetVisibleWidth() { return this->line_width; }
    int GetVisibleHeight() { return this->visible_height; }

    RECT FindBbByCharIdx(int idx) {
      RECT ret;
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

    static void StaticInit();

    MyMessageBox() { }
    ~MyMessageBox() { }

    void SetText(const std::wstring& msg, int width_limit);
    void CalculateDimension();
    void Draw();
    void Hide() { w = h = 0; x = -999; y = -999; }
  };