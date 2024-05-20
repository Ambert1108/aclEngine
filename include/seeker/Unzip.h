/**
 * Copyright (c) 2022, SeekLoud Team.
 * Date: 2022年8月17日
        * Main Developer: 杨宇航
        * Developer: Ambert @2023.12.25
 * Description: 解压文件demo
 * Refer:https://blog.csdn.net/auccy/article/details/81194838
 */

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#if _WIN32
#include <unzip.h>
#include <direct.h>
#include <io.h>
#else
#include <cstring>
#include <sys/stat.h>
#include <unzip.h>
#include <unistd.h>
#include <dirent.h>
#endif

#define MAX_PATH_LEN 256
#ifdef _WIN32
#define ACCESS(fileName,accessMode) _access(fileName,accessMode)
#define MKDIR(path) _mkdir(path)
#define BACKTICK "\\"
#else
#define ACCESS(fileName,accessMode) access(fileName,accessMode)
#define MKDIR(path) mkdir(path,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define BACKTICK "/"
#endif


namespace seeker {
  class Unzip {
  public:
    static bool createDir(const std::string& dir) {
      uint32_t dirPathLen = dir.length();
      if (dirPathLen > MAX_PATH_LEN) return false;
      char tmpDirPath[MAX_PATH_LEN] = { 0 };
      for (uint32_t i = 0; i < dirPathLen; ++i) {
        tmpDirPath[i] = dir[i];
        if (tmpDirPath[i] == '\\' || tmpDirPath[i] == '/') {
          if (ACCESS(tmpDirPath, 0) != 0) {
            int32_t ret = MKDIR(tmpDirPath);
            if (ret != 0) return false;
          }
        }
      }
      return true;
    }

    static bool unzipCurrentFile(unzFile uf, const char* destFolder) {
      char szFilePath[512];
      unz_file_info64 FileInfo;

      if (unzGetCurrentFileInfo64(uf, &FileInfo, szFilePath, sizeof(szFilePath), NULL, 0, NULL, 0) != UNZ_OK)
        return false;
      size_t len = strlen(szFilePath);
      if (len <= 0) return false;
      std::string fullFileName = destFolder;
      //linux 上不需要这个 \\ 会给文件夹前面加上一个/

      //给destFolder创建文件夹
      //判断文件夹是否存在
      if (ACCESS(fullFileName.c_str(), 0) != 0) {
        if (!createDir(fullFileName)) E_LOG("make dir={} failed", fullFileName);
      }
      fullFileName = fullFileName + BACKTICK + szFilePath;

      if (szFilePath[len - 1] == '\\' || szFilePath[len - 1] == '/') {
        MKDIR(fullFileName.c_str());
        E_LOG("fullFileName 2 = {}", fullFileName);
        return true;
      }
      auto file = fopen(fullFileName.c_str(), "wb+");

      if (file == nullptr) return false;

      const int BUFFER_SIZE = 4096;
      unsigned char byBuffer[BUFFER_SIZE];
      if (unzOpenCurrentFile(uf) != UNZ_OK) {
        fclose(file);
        return false;
      }
      while (true) {
        int nSize = unzReadCurrentFile(uf, byBuffer, BUFFER_SIZE);
        if (nSize < 0) {
          unzCloseCurrentFile(uf);
          fclose(file);
          return false;
        }
        else if (nSize == 0) break;
        else {
          size_t wSize = fwrite(byBuffer, 1, nSize, file);
          if (wSize != nSize) {
            unzCloseCurrentFile(uf);
            fclose(file);
            return false;
          }
        }
      }

      unzCloseCurrentFile(uf);
      fclose(file);
      return true;
    }

    static bool unzipFile(std::string zipFilePath, std::string goalFilePath) {
      I_LOG("UnzipFilePath={}, targetFilePath={}", zipFilePath, goalFilePath);
      if (ACCESS(goalFilePath.c_str(), 0) == 0) {
        W_LOG("file dir={} is exist", goalFilePath);
        return true;
      }

      unzFile uf = unzOpen64(zipFilePath.c_str());
      if (uf == NULL) return false;
      unz_global_info64 gi;
      if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
        unzClose(uf);
        return false;
      }

      std::string path = zipFilePath;
      auto pos = path.find_last_of("/\\");
      if (pos != std::string::npos) path.erase(path.begin() + pos, path.end());

      for (int i = 0; i < gi.number_entry; ++i) {
        if (!unzipCurrentFile(uf, goalFilePath.c_str())) {
          I_LOG("unzipCurrentFile false");
          unzClose(uf);
          return false;
        }
        if (i < gi.number_entry - 1) {
          if (unzGoToNextFile(uf) != UNZ_OK) {
            unzClose(uf);
            return false;
          }
        }
      }
      unzClose(uf);
      return true;
    }

    static bool rename_PNG(std::string pngPath, int num) {
        std::string oldName;
        std::string newName;
        std::string img1;
        //01.png
        img1 = pngPath + "/" + "01.png";
        auto file1 = fopen(img1.c_str(), "r");

        if (file1 == nullptr) {
            D_LOG("not 01.png");
        }
        else {
            fclose(file1);
            D_LOG("01.png");
            for (int j = 1; j < 10 && j <= num; j++) {
                oldName = pngPath + "/0" + std::to_string(j) + ".png";
                newName = pngPath + "/" + std::to_string(j) + ".png";
                int ret = rename(oldName.c_str(), newName.c_str());
            }
        }
        img1 = pngPath + "/" + "001.png";
        auto file2 = fopen(img1.c_str(), "r");

        if (file2 == nullptr) {
            D_LOG("not 001.png");
        }
        else {
            fclose(file2);
            for (int j = 1; j < 10 && j <= num; j++) {
                oldName = pngPath + "/00" + std::to_string(j) + ".png";
                newName = pngPath + "/" + std::to_string(j) + ".png";
                int ret = rename(oldName.c_str(), newName.c_str());
            }
            for (int j = 10; j < 100 && j <= num; j++) {
                oldName = pngPath + "/0" + std::to_string(j) + ".png";
                newName = pngPath + "/" + std::to_string(j) + ".png";
                int ret = rename(oldName.c_str(), newName.c_str());
            }
        }
        return true;
    }
  };
  class FileNum {
  public:
    static int getFileNum(const std::string& inPath)
    {
      int fileNum = 0;

      std::vector<std::string> pathVec;
      std::queue<std::string> q;
      q.push(inPath);
#if _WIN32
      while (!q.empty())
      {
        std::string item = q.front(); q.pop();

        std::string path = item + "\\*";
        struct _finddata_t fileinfo;
        auto handle = _findfirst(path.c_str(), &fileinfo);
        if (handle == -1) continue;

        while (!_findnext(handle, &fileinfo))
        {
          if (fileinfo.attrib & _A_SUBDIR)
          {
            if (strcmp(fileinfo.name, ".") == 0 || strcmp(fileinfo.name, "..") == 0)continue;
            q.push(item + "\\" + fileinfo.name);
      }
          else
          {
            fileNum++;
            pathVec.push_back(item + "\\" + fileinfo.name);
          }
    }
        _findclose(handle);
  }
#else
      DIR* pDir;
      struct dirent* ptr;
      if (!(pDir = opendir(inPath.c_str())))
        return fileNum;
      while ((ptr = readdir(pDir)) != 0) {
        if (strcmp(ptr->d_name, ".") != 0 && strcmp(ptr->d_name, "..") != 0)
          fileNum++;
      }
      closedir(pDir);

#endif
      return fileNum;
}
  };
}