#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

static FILE* g_log=nullptr; static std::mutex g_mu;
static void logf(const char* f, ...){ std::lock_guard<std::mutex> l(g_mu); if(!g_log)return; va_list a; va_start(a,f); vfprintf(g_log,f,a); va_end(a); fflush(g_log); }
static bool interesting(const char* n){ if(!n)return false; const char* k[]={"TextOut","ExtTextOut","DrawText","DrawTextEx","GetGlyphOutline","CreateFont","SelectObject","BitBlt","StretchBlt","TransparentBlt","AlphaBlend","SetDIBitsToDevice","StretchDIBits","GetDIBits","DirectDraw","Direct3D","D3D","Present","DWrite","DirectWrite"}; for(auto s:k)if(strstr(n,s))return true; return false; }

using GPA=FARPROC(WINAPI*)(HMODULE,LPCSTR); using LLA=HMODULE(WINAPI*)(LPCSTR); using LLW=HMODULE(WINAPI*)(LPCWSTR);
static GPA realGPA=nullptr; static LLA realLLA=nullptr; static LLW realLLW=nullptr;

extern "C" FARPROC WINAPI Hook_GetProcAddress(HMODULE h,LPCSTR name){
 FARPROC p=realGPA?realGPA(h,name):nullptr;
 if(name && IS_INTRESOURCE(name)){ logf("[GetProcAddress] ordinal=%u -> %p\n",(unsigned)(uintptr_t)name,(void*)p); }
 else if(name && interesting(name)){ char m[MAX_PATH]="?"; if(h)GetModuleFileNameA(h,m,MAX_PATH); logf("[GetProcAddress] module=%s name=%s -> %p\n",m,name,(void*)p); }
 return p;
}
extern "C" HMODULE WINAPI Hook_LoadLibraryA(LPCSTR n){ HMODULE h=realLLA?realLLA(n):nullptr; logf("[LoadLibraryA] %s -> %p\n",n?n:"(null)",(void*)h); return h; }
extern "C" HMODULE WINAPI Hook_LoadLibraryW(LPCWSTR n){ HMODULE h=realLLW?realLLW(n):nullptr; char b[512]="?"; if(n)WideCharToMultiByte(CP_UTF8,0,n,-1,b,sizeof(b),nullptr,nullptr); logf("[LoadLibraryW] %s -> %p\n",b,(void*)h); return h; }

BOOL APIENTRY DllMain(HMODULE h,DWORD r,LPVOID){
 if(r==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h); char p[MAX_PATH]{}; GetTempPathA(MAX_PATH,p); strcat_s(p,"NOBU6HD_GDI_TRACE_V3.log"); fopen_s(&g_log,p,"a");
  HMODULE k=GetModuleHandleA("kernel32.dll");
  realGPA=(GPA)GetProcAddress(k,"GetProcAddress"); realLLA=(LLA)GetProcAddress(k,"LoadLibraryA"); realLLW=(LLW)GetProcAddress(k,"LoadLibraryW");
  logf("\n=== NOBU6HD GDI TRACE V3 START ===\nPID=%lu\n",GetCurrentProcessId());
  logf("[INIT] GetProcAddress=%p LoadLibraryA=%p LoadLibraryW=%p\n",(void*)realGPA,(void*)realLLA,(void*)realLLW);
  logf("[NOTE] V3 exports hooks for the existing IAT/launcher layer.\n");
 } else if(r==DLL_PROCESS_DETACH){ logf("=== NOBU6HD GDI TRACE V3 END ===\n"); if(g_log){fclose(g_log);g_log=nullptr;} } return TRUE; }