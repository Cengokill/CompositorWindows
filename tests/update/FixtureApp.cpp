#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#ifndef HEALTH_RESULT
#define HEALTH_RESULT 0
#endif
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--update-health-check"){
        wchar_t log[32768]{};if(GetEnvironmentVariableW(L"COMPOSITOR_TEST_HEALTH_LOG",log,32768)){HANDLE file=CreateFileW(log,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){DWORD n=0;WriteFile(file,"health\n",7,&n,nullptr);CloseHandle(file);}}
        return HEALTH_RESULT;
    }
    if(argc>=3&&std::wstring(argv[1])==L"--record"){
        HANDLE file=CreateFileW(argv[2],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 9;std::wstring text=L"\xFEFF";for(int i=3;i<argc;++i){text+=argv[i];text+=L'\n';}DWORD n=0;BOOL ok=WriteFile(file,text.data(),DWORD(text.size()*sizeof(wchar_t)),&n,nullptr);CloseHandle(file);return ok?0:10;
    }return 0;
}
