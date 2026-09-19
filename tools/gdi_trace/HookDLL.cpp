#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string>
#include <mutex>
#include <sstream>
#include <intrin.h>

#pragma comment(lib, "psapi.lib")

namespace {
std::mutex g_mutex;
std::wstring g_log;
using ExtTextOutA_Fn = BOOL (WINAPI*)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*);
ExtTextOutA_Fn g_original = nullptr;
thread_local bool g_in_hook = false;

std::wstring Hex(UINT_PTR v){std::wstringstream s;s<<L"0x"<<std::hex<<std::uppercase<<v;return s.str();}
void Log(const std::wstring& s){
 std::lock_guard<std::mutex> lock(g_mutex);
 HANDLE f=CreateFileW(g_log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return; std::wstring x=s+L"\r\n"; DWORD n=0; WriteFile(f,x.data(),(DWORD)(x.size()*sizeof(wchar_t)),&n,nullptr); CloseHandle(f);
}
std::wstring CP932(const char* p,int n){
 if(!p||n<=0)return L"";
 int z=MultiByteToWideChar(932,0,p,n,nullptr,0); if(z<=0)return L"";
 std::wstring s(z,L'\0'); MultiByteToWideChar(932,0,p,n,s.data(),z); return s;
}
std::wstring ModuleName(HMODULE m){
 wchar_t p[MAX_PATH]{}; if(!m||!GetModuleFileNameW(m,p,MAX_PATH))return L"<unknown>";
 std::wstring s(p); size_t x=s.find_last_of(L"\\/"); return x==std::wstring::npos?s:s.substr(x+1);
}
void LogCall(LPCSTR text,UINT n,HDC h,int x,int y,UINT options){
 std::wstring s=CP932(text,(int)n);
 Log(L"[ExtTextOutA] text=\""+s+L"\" bytes="+std::to_wstring(n)+
     L" hdc="+Hex((UINT_PTR)h)+L" x="+std::to_wstring(x)+L" y="+std::to_wstring(y)+
     L" options="+Hex(options));
}

BOOL WINAPI HookExtTextOutA(HDC h,int x,int y,UINT o,const RECT*r,LPCSTR t,UINT n,const INT*d){
 if(!g_in_hook){g_in_hook=true;LogCall(t,n,h,x,y,o);g_in_hook=false;}
 return g_original(h,x,y,o,r,t,n,d);
}

bool PatchSlot(void** slot){
 if(!slot)return false;
 void* target=(void*)g_original;
 if(!target)return false;
 if(*slot==(void*)&HookExtTextOutA)return false;
 if(*slot!=target)return false;
 DWORD old=0; if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
 *slot=(void*)&HookExtTextOutA; DWORD tmp=0; VirtualProtect(slot,sizeof(void*),old,&tmp);
 FlushInstructionCache(GetCurrentProcess(),slot,sizeof(void*)); return true;
}

bool PatchModule(HMODULE mod){
 BYTE* base=(BYTE*)mod; auto* dos=(PIMAGE_DOS_HEADER)base;
 if(!dos||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 auto* nt=(PIMAGE_NT_HEADERS)(base+dos->e_lfanew); if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
 auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if(!dir.VirtualAddress)return false;
 auto* d=(PIMAGE_IMPORT_DESCRIPTOR)(base+dir.VirtualAddress); bool changed=false;
 for(;d->Name;++d){
  auto* thunk=(PIMAGE_THUNK_DATA)(base+d->FirstThunk); if(!thunk)continue;
  for(;thunk->u1.Function;++thunk){ changed|=PatchSlot((void**)&thunk->u1.Function); }
 }
 return changed;
}

DWORD WINAPI Worker(LPVOID){
 wchar_t temp[MAX_PATH]{}; DWORD n=GetTempPathW(MAX_PATH,temp);
 g_log=(n&&n<MAX_PATH)?std::wstring(temp)+L"nobu_exttext_trace.txt":L"C:\\Temp\\nobu_exttext_trace.txt";
 DeleteFileW(g_log.c_str());
 Log(L"=== NOBU6HD ExtTextOutA TRACE V1 START ===");
 Log(L"PID="+std::to_wstring(GetCurrentProcessId()));

 HMODULE gdi=GetModuleHandleW(L"gdi32.dll");
 for(int i=0;i<100 && !gdi;i++){Sleep(50);gdi=GetModuleHandleW(L"gdi32.dll");}
 if(!gdi){Log(L"[ERROR] gdi32.dll not loaded");return 0;}
 g_original=(ExtTextOutA_Fn)GetProcAddress(gdi,"ExtTextOutA");
 Log(L"[TARGET] gdi32!ExtTextOutA="+Hex((UINT_PTR)g_original));

 DWORD total=0;
 for(int pass=0;pass<120;pass++){
  HMODULE mods[2048]{}; DWORD bytes=0;
  if(EnumProcessModules(GetCurrentProcess(),mods,sizeof(mods),&bytes)){
   unsigned count=bytes/sizeof(HMODULE);
   for(unsigned i=0;i<count;i++)if(PatchModule(mods[i])){++total;Log(L"[IAT PATCH] module="+ModuleName(mods[i])+L" total="+std::to_wstring(total));}
  }
  Sleep(100);
 }
 Log(L"[DONE] IAT patches="+std::to_wstring(total));
 return 0;
}
}
BOOL APIENTRY DllMain(HMODULE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(t)CloseHandle(t);}
 return TRUE;
}
