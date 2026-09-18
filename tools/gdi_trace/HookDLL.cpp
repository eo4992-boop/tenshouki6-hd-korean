#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstring>
#pragma comment(lib, "psapi.lib")

namespace {
std::mutex g_mutex;
std::wstring g_path;
enum C { TOA,TOW,ETOA,ETOW,DTA,DTW,GA,GW,CFA,CFW,CFIA,CFIW,STOCK,SELECT,DIBITS,NCOUNT };
DWORD g_count[NCOUNT]{};

std::wstring LogPath(){ wchar_t b[MAX_PATH]{}; DWORD n=GetTempPathW(MAX_PATH,b); if(!n||n>=MAX_PATH) return L"C:\\Temp\\nobu_gdi_trace.txt"; return std::wstring(b)+L"nobu_gdi_trace.txt"; }
void Log(const std::wstring&s){ std::lock_guard<std::mutex> l(g_mutex); HANDLE h=CreateFileW(g_path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); if(h==INVALID_HANDLE_VALUE)return; std::wstring x=s+L"\r\n"; DWORD w=0; WriteFile(h,x.data(),(DWORD)(x.size()*sizeof(wchar_t)),&w,nullptr); CloseHandle(h); }
std::wstring Hex(UINT_PTR v){ std::wstringstream s; s<<L"0x"<<std::hex<<std::uppercase<<v; return s.str(); }
std::wstring A2W(const char*s,int n){ if(!s)return L""; if(n<0)n=lstrlenA(s); if(!n)return L""; int m=MultiByteToWideChar(932,0,s,n,nullptr,0); if(!m)m=MultiByteToWideChar(CP_ACP,0,s,n,nullptr,0); if(!m)return L""; std::wstring o(m,L'\0'); MultiByteToWideChar(932,0,s,n,o.data(),m); return o; }
std::wstring W(const wchar_t*s,int n){ if(!s)return L""; if(n<0)n=lstrlenW(s); return std::wstring(s,s+n); }
void LT(const wchar_t*a,const wchar_t*s,int n,DWORD&i){++i; Log(L"["+std::wstring(a)+L"] text=\""+W(s,n)+L"\" len="+std::to_wstring(n));}
void LA(const wchar_t*a,const char*s,int n,DWORD&i){++i; Log(L"["+std::wstring(a)+L"] text=\""+A2W(s,n)+L"\" len="+std::to_wstring(n));}
using FTOA=BOOL(WINAPI*)(HDC,int,int,LPCSTR,int); using FTOW=BOOL(WINAPI*)(HDC,int,int,LPCWSTR,int);
using FETOA=BOOL(WINAPI*)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*); using FETOW=BOOL(WINAPI*)(HDC,int,int,UINT,const RECT*,LPCWSTR,UINT,const INT*);
using FDTA=int(WINAPI*)(HDC,LPCSTR,int,LPRECT,UINT); using FDTW=int(WINAPI*)(HDC,LPCWSTR,int,LPRECT,UINT);
using FGA=DWORD(WINAPI*)(HDC,UINT,UINT,LPGLYPHMETRICS,DWORD,LPVOID,const MAT2*); using FGW=FGA;
using FCFA=HFONT(WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR); using FCFW=HFONT(WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCWSTR);
using FCFIA=HFONT(WINAPI*)(const LOGFONTA*); using FCFIW=HFONT(WINAPI*)(const LOGFONTW*); using FGS=HGDIOBJ(WINAPI*)(int); using FSO=HGDIOBJ(WINAPI*)(HDC,HGDIOBJ); using FGD=int(WINAPI*)(HDC,HBITMAP,UINT,UINT,LPVOID,const BITMAPINFO*,UINT);
struct O{FTOA toa{};FTOW tow{};FETOA etoa{};FETOW etow{};FDTA dta{};FDTW dtw{};FGA ga{};FGW gw{};FCFA cfa{};FCFW cfw{};FCFIA cfia{};FCFIW cfiw{};FGS gs{};FSO so{};FGD gd{}}g;
BOOL WINAPI HTOA(HDC h,int x,int y,LPCSTR s,int n){LA(L"TextOutA",s,n,g_count[TOA]);return g.toa(h,x,y,s,n);} BOOL WINAPI HTOW(HDC h,int x,int y,LPCWSTR s,int n){LT(L"TextOutW",s,n,g_count[TOW]);return g.tow(h,x,y,s,n);}
BOOL WINAPI HETOA(HDC h,int x,int y,UINT o,const RECT*r,LPCSTR s,UINT n,const INT*d){LA(L"ExtTextOutA",s,(int)n,g_count[ETOA]);return g.etoa(h,x,y,o,r,s,n,d);} BOOL WINAPI HETOW(HDC h,int x,int y,UINT o,const RECT*r,LPCWSTR s,UINT n,const INT*d){LT(L"ExtTextOutW",s,(int)n,g_count[ETOW]);return g.etow(h,x,y,o,r,s,n,d);}
int WINAPI HDTA(HDC h,LPCSTR s,int n,LPRECT r,UINT f){LA(L"DrawTextA",s,n,g_count[DTA]);return g.dta(h,s,n,r,f);} int WINAPI HDTW(HDC h,LPCWSTR s,int n,LPRECT r,UINT f){LT(L"DrawTextW",s,n,g_count[DTW]);return g.dtw(h,s,n,r,f);}
DWORD WINAPI HGA(HDC h,UINT c,UINT f,LPGLYPHMETRICS m,DWORD cb,LPVOID b,const MAT2*mat){++g_count[GA];Log(L"[GetGlyphOutlineA] char="+Hex(c)+L" flags="+Hex(f));return g.ga(h,c,f,m,cb,b,mat);} DWORD WINAPI HGW(HDC h,UINT c,UINT f,LPGLYPHMETRICS m,DWORD cb,LPVOID b,const MAT2*mat){++g_count[GW];Log(L"[GetGlyphOutlineW] char="+Hex(c)+L" flags="+Hex(f));return g.gw(h,c,f,m,cb,b,mat);}
HFONT WINAPI HCFA(int a,int b,int c,int d,int e,DWORD f,DWORD q,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,LPCSTR n){++g_count[CFA];Log(L"[CreateFontA] face=\""+A2W(n,-1)+L"\"");return g.cfa(a,b,c,d,e,f,q,h,i,j,k,l,m,n);} HFONT WINAPI HCFW(int a,int b,int c,int d,int e,DWORD f,DWORD q,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,LPCWSTR n){++g_count[CFW];Log(L"[CreateFontW] face=\""+std::wstring(n?n:L"")+L"\"");return g.cfw(a,b,c,d,e,f,q,h,i,j,k,l,m,n);}
HFONT WINAPI HCFIA(const LOGFONTA*p){++g_count[CFIA];Log(L"[CreateFontIndirectA]");return g.cfia(p);} HFONT WINAPI HCFIW(const LOGFONTW*p){++g_count[CFIW];Log(L"[CreateFontIndirectW]");return g.cfiw(p);}
HGDIOBJ WINAPI HGS(int t){++g_count[STOCK];if(t==17)Log(L"[GetStockObject] type=17");return g.gs(t);} HGDIOBJ WINAPI HSO(HDC h,HGDIOBJ o){++g_count[SELECT];return g.so(h,o);} int WINAPI HGD(HDC h,HBITMAP b,UINT s,UINT c,LPVOID p,const BITMAPINFO*i,UINT u){++g_count[DIBITS];Log(L"[GetDIBits] start="+std::to_wstring(s)+L" lines="+std::to_wstring(c));return g.gd(h,b,s,c,p,i,u);}

bool Patch(HMODULE mod){BYTE*base=(BYTE*)mod;auto dos=(PIMAGE_DOS_HEADER)base;if(!dos||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;auto nt=(PIMAGE_NT_HEADERS)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;auto&di=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!di.VirtualAddress)return false;auto imp=(PIMAGE_IMPORT_DESCRIPTOR)(base+di.VirtualAddress);bool changed=false;for(;imp->Name;++imp){const char*dll=(const char*)(base+imp->Name);if(_stricmp(dll,"gdi32.dll")&&_stricmp(dll,"user32.dll"))continue;if(!imp->OriginalFirstThunk)continue;auto th=(PIMAGE_THUNK_DATA)(base+imp->FirstThunk);auto og=(PIMAGE_THUNK_DATA)(base+imp->OriginalFirstThunk);for(;og->u1.AddressOfData;++og,++th){if(IMAGE_SNAP_BY_ORDINAL(og->u1.Ordinal))continue;auto ibn=(PIMAGE_IMPORT_BY_NAME)(base+og->u1.AddressOfData);const char*n=(const char*)ibn->Name;void**slot=(void**)&th->u1.Function;void*rep=nullptr;
if(!strcmp(n,"TextOutA")){if(!g.toa)g.toa=(FTOA)*slot;rep=(void*)HTOA;}else if(!strcmp(n,"TextOutW")){if(!g.tow)g.tow=(FTOW)*slot;rep=(void*)HTOW;}else if(!strcmp(n,"ExtTextOutA")){if(!g.etoa)g.etoa=(FETOA)*slot;rep=(void*)HETOA;}else if(!strcmp(n,"ExtTextOutW")){if(!g.etow)g.etow=(FETOW)*slot;rep=(void*)HETOW;}else if(!strcmp(n,"DrawTextA")){if(!g.dta)g.dta=(FDTA)*slot;rep=(void*)HDTA;}else if(!strcmp(n,"DrawTextW")){if(!g.dtw)g.dtw=(FDTW)*slot;rep=(void*)HDTW;}else if(!strcmp(n,"GetGlyphOutlineA")){if(!g.ga)g.ga=(FGA)*slot;rep=(void*)HGA;}else if(!strcmp(n,"GetGlyphOutlineW")){if(!g.gw)g.gw=(FGW)*slot;rep=(void*)HGW;}else if(!strcmp(n,"CreateFontA")){if(!g.cfa)g.cfa=(FCFA)*slot;rep=(void*)HCFA;}else if(!strcmp(n,"CreateFontW")){if(!g.cfw)g.cfw=(FCFW)*slot;rep=(void*)HCFW;}else if(!strcmp(n,"CreateFontIndirectA")){if(!g.cfia)g.cfia=(FCFIA)*slot;rep=(void*)HCFIA;}else if(!strcmp(n,"CreateFontIndirectW")){if(!g.cfiw)g.cfiw=(FCFIW)*slot;rep=(void*)HCFIW;}else if(!strcmp(n,"GetStockObject")){if(!g.gs)g.gs=(FGS)*slot;rep=(void*)HGS;}else if(!strcmp(n,"SelectObject")){if(!g.so)g.so=(FSO)*slot;rep=(void*)HSO;}else if(!strcmp(n,"GetDIBits")){if(!g.gd)g.gd=(FGD)*slot;rep=(void*)HGD;}
if(rep&&*slot!=rep){DWORD old;if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){*slot=rep;VirtualProtect(slot,sizeof(void*),old,&old);FlushInstructionCache(GetCurrentProcess(),slot,sizeof(void*));changed=true;}}}}return changed;}
DWORD WINAPI Worker(LPVOID){g_path=LogPath();DeleteFileW(g_path.c_str());Log(L"=== NOBU6HD GDI TRACE START ===");Log(L"PID="+std::to_wstring(GetCurrentProcessId()));HMODULE m[1024]{};DWORD cb=0,changed=0;if(EnumProcessModules(GetCurrentProcess(),m,sizeof(m),&cb)){unsigned n=cb/sizeof(HMODULE);for(unsigned i=0;i<n;++i)if(Patch(m[i]))++changed;}Log(L"[IAT] modules_changed="+std::to_wstring(changed));return 0;}
void Summary(){if(g_path.empty())return;Log(L"=== SUMMARY ===");const wchar_t*names[NCOUNT]={L"TextOutA",L"TextOutW",L"ExtTextOutA",L"ExtTextOutW",L"DrawTextA",L"DrawTextW",L"GetGlyphOutlineA",L"GetGlyphOutlineW",L"CreateFontA",L"CreateFontW",L"CreateFontIndirectA",L"CreateFontIndirectW",L"GetStockObject",L"SelectObject",L"GetDIBits"};for(int i=0;i<NCOUNT;++i)Log(std::wstring(names[i])+L"="+std::to_wstring(g_count[i]));}
}
BOOL APIENTRY DllMain(HMODULE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);CreateThread(nullptr,0,Worker,nullptr,0,nullptr);}else if(reason==DLL_PROCESS_DETACH){Summary();}return TRUE;}
