#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

DWORD FindProcess(const wchar_t* exe){
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(s==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W p{sizeof(p)}; DWORD pid=0;
    if(Process32FirstW(s,&p)){do{if(!_wcsicmp(p.szExeFile,exe)){pid=p.th32ProcessID;break;}}while(Process32NextW(s,&p));}
    CloseHandle(s); return pid;
}

int wmain(){
    std::wcout<<L"NOBU6HD GDI Trace Injector V2\n";
    DWORD pid=FindProcess(L"NOBU6HD_JP.exe");
    if(!pid){std::wcerr<<L"ERROR: NOBU6HD_JP.exe is not running. Start the game first.\n";return 1;}
    wchar_t dll[MAX_PATH]{};
    DWORD n=GetFullPathNameW(L"HookDLL.dll",MAX_PATH,dll,nullptr);
    if(!n||n>=MAX_PATH){std::wcerr<<L"ERROR: HookDLL.dll not found in the current folder.\n";return 2;}
    std::wcout<<L"Target PID: "<<pid<<L"\nDLL: "<<dll<<L"\n";
    HANDLE p=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
    if(!p){std::wcerr<<L"ERROR: OpenProcess failed. Win32 error="<<GetLastError()<<L"\n";return 3;}
    SIZE_T bytes=(wcslen(dll)+1)*sizeof(wchar_t);
    void* mem=VirtualAllocEx(p,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!mem){std::wcerr<<L"ERROR: VirtualAllocEx failed. Win32 error="<<GetLastError()<<L"\n";CloseHandle(p);return 4;}
    if(!WriteProcessMemory(p,mem,dll,bytes,nullptr)){std::wcerr<<L"ERROR: WriteProcessMemory failed. Win32 error="<<GetLastError()<<L"\n";VirtualFreeEx(p,mem,0,MEM_RELEASE);CloseHandle(p);return 5;}
    auto load=(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    if(!load){std::wcerr<<L"ERROR: LoadLibraryW address not found.\n";VirtualFreeEx(p,mem,0,MEM_RELEASE);CloseHandle(p);return 6;}
    HANDLE t=CreateRemoteThread(p,nullptr,0,load,mem,0,nullptr);
    if(!t){std::wcerr<<L"ERROR: CreateRemoteThread failed. Win32 error="<<GetLastError()<<L"\n";VirtualFreeEx(p,mem,0,MEM_RELEASE);CloseHandle(p);return 7;}
    DWORD wait=WaitForSingleObject(t,5000);
    if(wait==WAIT_TIMEOUT)std::wcerr<<L"WARNING: injection thread did not finish within 5 seconds.\n";
    DWORD ec=0; GetExitCodeThread(t,&ec);
    VirtualFreeEx(p,mem,0,MEM_RELEASE); CloseHandle(t); CloseHandle(p);
    if(!ec){std::wcerr<<L"ERROR: DLL load failed. Thread exit code=0\n";return 8;}
    std::wcout<<L"SUCCESS: DLL injected.\n";
    std::wcout<<L"Log: "<<dll<<L"\n";
    std::wcout<<L"Press Enter to close this window.\n";
    std::wstring dummy; std::getline(std::wcin,dummy);
    return 0;
}
