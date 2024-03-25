#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <strsafe.h>
#include <string>
#include <atlstr.h>
#include "../DLL1/miniz.h"

#include "../DLL1/config.h"

mz_zip_archive g_archive;

void getFiles(CString directory, bool preserve_folder_name = false) {
  HANDLE dir;
  WIN32_FIND_DATA file_data;
  CString  file_name, full_file_name;
  if ((dir = FindFirstFile((directory + "/*"), &file_data)) == INVALID_HANDLE_VALUE)
  {
    // Invalid directory
  }

  int idx = 1;
  while (FindNextFile(dir, &file_data)) {
    file_name = file_data.cFileName;
    full_file_name = directory + '\\' + file_name;
    if (strcmp(file_data.cFileName, ".") != 0 && strcmp(file_data.cFileName, "..") != 0)
    {
      std::string fileName = full_file_name.GetString();
      // Do stuff with fileName
      const int N = fileName.size();
      int n = N-1;
      while (n > 0 && (fileName[n] != '\\' && fileName[n] != '/')) n--;
      std::string fn1 = fileName.substr(n + 1);
      if ((fileName.rfind(".txt") == N - 4) || (fileName.rfind(".png") == N - 4)) {
        int ret = -999;
        if (!preserve_folder_name)
          ret = mz_zip_writer_add_file(&g_archive, fn1.c_str(), fileName.c_str(), "", 0, MZ_BEST_SPEED);
        else
          ret = mz_zip_writer_add_file(&g_archive, fileName.c_str(), fileName.c_str(), "", 0, MZ_BEST_SPEED);
        printf("%d): %s %d             \r", idx, fileName.c_str(), ret);
        idx++;
      }
    }
  }

  printf("\nDone\n");
}

PtgOverlayConfig g_config;

int main(int argc, char** argv) {
  g_config.Load();

  printf("Select operation mode:\n");
  printf(" 1) inverted index\n");
  printf(" 2) aligned dialog text\n");
  printf(" 3) parallel text\n");
  printf(" 4) match templates\n");
  printf(" 5) miscellaneous files\n");
  printf(" Any other key) do all of them\n");
  int k = getchar();

  int ch = 0;
  switch (k) {
    case '1': ch = 1; break;
    case '2': ch = 2; break;
    case '3': ch = 3; break;
    case '4': ch = 4; break;
    case '5': ch = 5; break;
    default: break;
  }

  if (ch == 0 || ch == 1) {
    printf("Packing inverted index ...\n");
    mz_zip_writer_init_file(&g_archive, (g_config.root_path + "inv_idx_zipped.gz").c_str(), 1024 * 1024);
    getFiles((g_config.root_path + "\\dialog_aligned\\inverted_index").c_str());
    mz_zip_writer_finalize_archive(&g_archive);
    mz_zip_writer_end(&g_archive);
  }

  if (ch == 0 || ch == 2) {
    printf("Packing dialog files ...\n");
    mz_zip_writer_init_file(&g_archive, (g_config.root_path + "\\dialog_aligned.gz").c_str(), 1024 * 1024);
    getFiles((g_config.root_path + "\\dialog_aligned\\").c_str());
    mz_zip_writer_finalize_archive(&g_archive);
    mz_zip_writer_end(&g_archive);
  }

  if (ch == 0 || ch == 3) {
    printf("Packing parallel text ...\n");
    mz_zip_writer_init_file(&g_archive, (g_config.root_path + "\\parallel_text.gz").c_str(), 1024 * 1024);
    getFiles((g_config.root_path + "\\parallel_text\\").c_str());
    mz_zip_writer_finalize_archive(&g_archive);
    mz_zip_writer_end(&g_archive);
  }

  if (ch == 0 || ch == 4) {
    printf("Packing match templates ...\n");
    mz_zip_writer_init_file(&g_archive, (g_config.root_path + "\\match_templates.gz").c_str(), 1024 * 1024);
    
    getFiles((g_config.root_path + "\\MatchTemplates\\").c_str());
    mz_zip_writer_add_file(&g_archive, "TemplateList.txt", (g_config.root_path + "\\TemplateList.txt").c_str(), "", 0, MZ_BEST_COMPRESSION);
    mz_zip_writer_finalize_archive(&g_archive);
    mz_zip_writer_end(&g_archive);
  }

  if (ch == 0 || ch == 5) {
    printf("Packing miscellaneous files ...\n");
    mz_zip_writer_init_file(&g_archive, (g_config.root_path + "\\miscellaneous_files.gz").c_str(), 1024 * 1024);
    mz_zip_writer_add_file(&g_archive, "ProperNames.txt", (g_config.root_path + "\\ProperNames.txt").c_str(), "", 0, MZ_BEST_COMPRESSION);
    mz_zip_writer_add_file(&g_archive, "vs_intro.txt", (g_config.root_path + "\\VideoSubtitles\\vs_intro.txt").c_str(), "", 0, MZ_BEST_COMPRESSION);
    mz_zip_writer_add_file(&g_archive, "vs_credits.txt", (g_config.root_path + "\\VideoSubtitles\\vs_credits.txt").c_str(), "", 0, MZ_BEST_COMPRESSION);
    mz_zip_writer_finalize_archive(&g_archive);
    mz_zip_writer_end(&g_archive);
  }

  printf("Done!\n");
}