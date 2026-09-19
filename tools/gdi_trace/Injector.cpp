#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>

bool Inject(HANDLE p,const wchar_t* dll){
 SIZE_T bytes=(wcslen(dll)+1)*sizeof(wchar_t);
 void* mem=VirtualAllocEx(p,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!mem)return false;
 bool ok=WriteProcessMemory(p,mem,dll,bytes,nullptr);
 auto load=(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
 HANDLE t=ok&&load?CreateRemoteThread(p,nullptr,0,load,mem,0,nullptr):nullptr;
 if(!t){VirtualFreeEx(p,mem,0,MEM_RELEASE);return false;}
 WaitForSingleObject(t,5000); DWORD ec=0;GetExitCodeThread(t,&ec);
 CloseHandle(t);VirtualFreeEx(p,mem,0,MEM_RELEASE); return ec!=0;
}

int wmain(){
 std::wcout<<L"NOBU6HD ExtTextOutA Trace V1\n";
 wchar_t dll[MAX_PATH]{}; DWORD n=GetFullPathNameW(L"HookDLL.dll",MAX_PATH,dll,nullptr);
 if(!n||n>=MAX_PATH){std::wcerr<<L"ERROR: HookDLL.dll not found.\n";return 1;}

 STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
 wchar_t cmd[]=L"NOBU6HD_JP.exe";
 if(!CreateProcessW(nullptr,cmd,nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)){
  std::wcerr<<L"ERROR: CreateProcess failed. Win32 error="<<GetLastError()<<L"\n";return 2;
 }
 std::wcout<<L"Target PID: "<<pi.dwProcessId<<L"\n";
 std::wcout<<L"Injecting before first instruction...\n";
 bool ok=Inject(pi.hProcess,dll);
 if(!ok){std::wcerr<<L"ERROR: DLL injection failed.\n";TerminateProcess(pi.hProcess,10);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 3;}
 ResumeThread(pi.hThread);
 CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
 std::wcout<<L"SUCCESS: game launched with ExtTextOutA tracer.\n";
 std::wcout<<L"Log: %TEMP%\\nobu_exttext_trace.txt\n";
 std::wcout<<L"Play until the Japanese text appears, then close the game.\n";
 return 0;
}
