#if _WIN32
//在windows系统下需要的一些库和宏
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

#pragma comment(lib, "libcurl_a.lib")

#define BUILDING_LIBCURL  
#define HTTP_ONLY         
#define CURL_STATICLIB    
#include "curl.h"
#include <direct.h>
#include <io.h>
#include <cstdio>
#else
#include <cstdio>
#include <unistd.h>
#include <curl/curl.h>
#endif // !WIN32

#include<iostream>
#include<string>


namespace seeker {

  //using std::string;

  class Download {
  public:
    //下载文件数据接收函数
    static size_t dl_req_reply(void* buffer, size_t size, size_t nmemb, void* user_p)
    {
      FILE* fp = (FILE*)user_p;
      size_t return_size = fwrite(buffer, size, nmemb, fp);
      //cout << (char *)buffer << endl;
      return return_size;
    }

    //http GET请求文件下载  
    static CURLcode dl_curl_get_req(const std::string& url, std::string filename, bool download = 1)
    {
      //int len = filename.length();
      //char* file_name = new char(len + 1);//char*最后有一个结束字符\0
      //strcpy_s(file_name, len + 1, filename.c_str());
      if (download) {
#if _WIN32
        //判断文件夹是否存在
        if (_access(filename.c_str(), 0) == 0)
          return static_cast<CURLcode>(101);
#else
        if (access(filename.c_str(), F_OK) == 0)
          return static_cast<CURLcode>(101);
#endif // _WIN32
      }
      const char* file_name = filename.c_str();
      char* pc = new char[1024];//足够长
      strcpy(pc, file_name);

      FILE* fp = fopen(pc, "wb");

      //curl初始化  
      CURL* curl = curl_easy_init();
      // curl返回值 
      CURLcode res;
      if (curl)
      {
        //设置curl的请求头
        struct curl_slist* header_list = NULL;
        header_list = curl_slist_append(header_list, "User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; rv:11.0) like Gecko");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        //不接收响应头数据0代表不接收 1代表接收
        curl_easy_setopt(curl, CURLOPT_HEADER, 0);

        //设置请求的URL地址 
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        //设置ssl验证
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

        //CURLOPT_VERBOSE的值为1时，会显示详细的调试信息
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);

        //设置数据接收函数
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &dl_req_reply);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

        //设置超时时间
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6); // set transport and time out time  
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6);

        // 开启请求  
        res = curl_easy_perform(curl);
      }
      // 释放curl 
      curl_easy_cleanup(curl);
      //释放文件资源
      fclose(fp);
      //用后释放
      if (pc != nullptr)
        delete[] pc;
      pc = nullptr;
      if (res != 0) {
        W_LOG("download fail, remove file");
        remove(filename.c_str());
      }
      return res;
      //返回0为成功
    }
  };

}  // namespace seeker