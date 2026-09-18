#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string>
#include <mutex>
#include <sstream>
#include <cstring>
#pragma comment(lib,"psapi.lib")
namespace {
std::mutex mx; std::wstring logp; bool ready=false;
enum C{TextOutA,TextOutW,ExtTextOutA,ExtTextOutW,DrawTextA,DrawTextW,GetGlyphOutlineA,GetGlyphOutlineW,CreateFontA,CreateFontW,CreateFontIndirectA,CreateFontIndirectW,GetStockObject,SelectObject,GetDIBits,BitBlt,StretchBlt,PatBlt,AlphaBlend,GetProcAddress,LoadLibraryA,LoadLibraryW,Count}; DWORD c[Count]{};
std::wstring A2W(const char*p,int n=-1){if(!p)return L"";if(n<0)n=lstrlenA(p);if(n<=0)return L"";int r=MultiByteToWideChar(932,0,p,n,nullptr,0);if(r<=0)return L"";std::wstring s(r,L'\0');MultiByteToWideChar(932,0,p,n,s.data(),r);return s;}
std::wstring H(UINT_PTR v){std::wstringstream s;s<<L"0x"<<std::hex<<std::uppercase<<v;return s.str();}
void Log(const std::wstring&s){if(!ready)return;std::lock_guard<std::mutex>l(mx);HANDLE f=CreateFileW(logp.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;std::wstring x=s+L"\r\n";DWORD w;WriteFile(f,x.data(),(DWORD)x.size()*sizeof(wchar_t),&w,nullptr);CloseHandle(f);}
std::wstring Mod(HMODULE m){wchar_t b[MAX_PATH]{};return GetModuleFileNameW(m,b,MAX_PATH)?b:L"<unknown>";}
using TA=BOOL(WINAPI*)(HDC,int,int,LPCSTR,int);using TW=BOOL(WINAPI*)(HDC,int,int,LPCWSTR,int);
using EA=BOOL(WINAPI*)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*);using EW=BOOL(WINAPI*)(HDC,int,int,UINT,const RECT*,LPCWSTR,UINT,const INT*);
using DA=int(WINAPI*)(HDC,LPCSTR,int,LPRECT,UINT);using DW=int(WINAPI*)(HDC,LPCWSTR,int,LPRECT,UINT);
using GA=DWORD(WINAPI*)(HDC,UINT,UINT,LPGLYPHMETRICS,DWORD,LPVOID,const MAT2*);using GW=GA;
using CFA=HFONT(WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR);using CFW=HFONT(WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCWSTR);
using CFIA=HFONT(WINAPI*)(const LOGFONTA*);using CFIW=HFONT(WINAPI*)(const LOGFONTW*);using GSO=HGDIOBJ(WINAPI*)(int);using SO=HGDIOBJ(WINAPI*)(HDC,HGDIOBJ);
using GD=int(WINAPI*)(HDC,HBITMAP,UINT,UINT,LPVOID,const BITMAPINFO*,UINT);using BB=BOOL(WINAPI*)(HDC,int,int,int,int,HDC,int,int,DWORD);
using SB=BOOL(WINAPI*)(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);using PB=BOOL(WINAPI*)(HDC,int,int,int,int,DWORD);using AB=BOOL(WINAPI*)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
using GPA=FARPROC(WINAPI*)(HMODULE,LPCSTR);using LLA=HMODULE(WINAPI*)(LPCSTR);using LLW=HMODULE(WINAPI*)(LPCWSTR);
struct O{TA ta{};TW tw{};EA ea{};EW ew{};DA da{};DW dw{};GA ga{};GW gw{};CFA cfa{};CFW cfw{};CFIA cfia{};CFIW cfiw{};GSO gso{};SO so{};GD gd{};BB bb{};SB sb{};PB pb{};AB ab{};GPA gpa{};LLA lla{};LLW llw{};}o;
template<class F>bool Patch(void**slot,F&orig,F repl){F*s=(F*)slot;if(*s==repl)return false;if(!orig)orig=*s;DWORD old;if(!VirtualProtect(s,sizeof(F),PAGE_READWRITE,&old))return false;*s=repl;DWORD z;VirtualProtect(s,sizeof(F),old,&z);FlushInstructionCache(GetCurrentProcess(),s,sizeof(F));return true;}
BOOL WINAPI HTA(HDC h,int x,int y,LPCSTR t,int n){++c[TextOutA];Log(L"[TextOutA] text=\""+A2W(t,n)+L"\" len="+std::to_wstring(n));return o.ta(h,x,y,t,n);}
BOOL WINAPI HTW(HDC h,int x,int y,LPCWSTR t,int n){++c[TextOutW];Log(L"[TextOutW] text=\""+std::wstring(t?t:L"",n<0?(t?lstrlenW(t):0):n)+L"\" len="+std::to_wstring(n));return o.tw(h,x,y,t,n);}
BOOL WINAPI HEA(HDC h,int x,int y,UINT f,const RECT*r,LPCSTR t,UINT n,const INT*d){++c[ExtTextOutA];Log(L"[ExtTextOutA] text=\""+A2W(t,(int)n)+L"\"");return o.ea(h,x,y,f,r,t,n,d);}
BOOL WINAPI HEW(HDC h,int x,int y,UINT f,const RECT*r,LPCWSTR t,UINT n,const INT*d){++c[ExtTextOutW];Log(L"[ExtTextOutW] text=\""+std::wstring(t?t:L"",n)+L"\"");return o.ew(h,x,y,f,r,t,n,d);}
int WINAPI HDA(HDC h,LPCSTR t,int n,LPRECT r,UINT f){++c[DrawTextA];Log(L"[DrawTextA] text=\""+A2W(t,n)+L"\"");return o.da(h,t,n,r,f);}
int WINAPI HDW(HDC h,LPCWSTR t,int n,LPRECT r,UINT f){++c[DrawTextW];Log(L"[DrawTextW] text=\""+std::wstring(t?t:L"",n<0?(t?lstrlenW(t):0):n)+L"\"");return o.dw(h,t,n,r,f);}
DWORD WINAPI HGA(HDC h,UINT ch,UINT f,LPGLYPHMETRICS m,DWORD z,LPVOID b,const MAT2*a){++c[GetGlyphOutlineA];Log(L"[GetGlyphOutlineA] char="+H(ch));return o.ga(h,ch,f,m,z,b,a);}
DWORD WINAPI HGW(HDC h,UINT ch,UINT f,LPGLYPHMETRICS m,DWORD z,LPVOID b,const MAT2*a){++c[GetGlyphOutlineW];Log(L"[GetGlyphOutlineW] char="+H(ch));return o.gw(h,ch,f,m,z,b,a);}
HFONT WINAPI HCFA(int a,int b,int d,int e,int f,DWORD g,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,DWORD n,LPCSTR face){++c[CreateFontA];Log(L"[CreateFontA] face=\""+A2W(face)+L"\"");return o.cfa(a,b,d,e,f,g,h,i,j,k,l,m,n,face);}
HFONT WINAPI HCFW(int a,int b,int d,int e,int f,DWORD g,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,DWORD n,LPCWSTR face){++c[CreateFontW];Log(L"[CreateFontW] face=\""+std::wstring(face?face:L"")+L"\"");return o.cfw(a,b,d,e,f,g,h,i,j,k,l,m,n,face);}
HFONT WINAPI HCFIA(const LOGFONTA*x){++c[CreateFontIndirectA];return o.cfia(x);} HFONT WINAPI HCFIW(const LOGFONTW*x){++c[CreateFontIndirectW];return o.cfiw(x);}
HGDIOBJ WINAPI HGSO(int x){++c[GetStockObject];return o.gso(x);} HGDIOBJ WINAPI HSO(HDC h,HGDIOBJ x){++c[SelectObject];return o.so(h,x);}
int WINAPI HGD(HDC h,HBITMAP b,UINT s,UINT n,LPVOID p,const BITMAPINFO*i,UINT u){++c[GetDIBits];return o.gd(h,b,s,n,p,i,u);}
BOOL WINAPI HBB(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,DWORD r){++c[BitBlt];return o.bb(a,x,y,w,h,b,sx,sy,r);}
BOOL WINAPI HSB(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,int sw,int sh,DWORD r){++c[StretchBlt];return o.sb(a,x,y,w,h,b,sx,sy,sw,sh,r);}
BOOL WINAPI HPB(HDC h,int x,int y,int w,int d,DWORD r){++c[PatBlt];return o.pb(h,x,y,w,d,r);}
BOOL WINAPI HAB(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,int sw,int sh,BLENDFUNCTION f){++c[AlphaBlend];return o.ab(a,x,y,w,h,b,sx,sy,sw,sh,f);}
FARPROC WINAPI HGPA(HMODULE m,LPCSTR n){++c[GetProcAddress];FARPROC r=o.gpa(m,n);if(n&&HIWORD(n)!=0)Log(L"[GetProcAddress] module="+Mod(m)+L" requested=\""+A2W(n)+L"\" result="+H((UINT_PTR)r));return r;}
HMODULE WINAPI HLLA(LPCSTR n){++c[LoadLibraryA];HMODULE r=o.lla(n);Log(L"[LoadLibraryA] name=\""+A2W(n)+L"\" result="+H((UINT_PTR)r));return r;}
HMODULE WINAPI HLLW(LPCWSTR n){++c[LoadLibraryW];HMODULE r=o.llw(n);Log(L"[LoadLibraryW] name=\""+std::wstring(n?n:L"")+L"\" result="+H((UINT_PTR)r));return r;}
bool PM(HMODULE mod){BYTE*b=(BYTE*)mod;auto*d=(PIMAGE_DOS_HEADER)b;if(!d||d->e_magic!=IMAGE_DOS_SIGNATURE)return false;auto*n=(PIMAGE_NT_HEADERS)(b+d->e_lfanew);if(n->Signature!=IMAGE_NT_SIGNATURE)return false;auto dir=n->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return false;auto*id=(PIMAGE_IMPORT_DESCRIPTOR)(b+dir.VirtualAddress);bool ch=false;for(;id->Name;++id){const char*dll=(const char*)(b+id->Name);bool g=!_stricmp(dll,"gdi32.dll"),u=!_stricmp(dll,"user32.dll"),k=!_stricmp(dll,"kernel32.dll");if((!g&&!u&&!k)||!id->OriginalFirstThunk)continue;auto*t=(PIMAGE_THUNK_DATA)(b+id->FirstThunk);auto*oig=(PIMAGE_THUNK_DATA)(b+id->OriginalFirstThunk);for(;oig->u1.AddressOfData;++oig,++t){if(IMAGE_SNAP_BY_ORDINAL(oig->u1.Ordinal))continue;auto*in=(PIMAGE_IMPORT_BY_NAME)(b+oig->u1.AddressOfData);const char*n2=(const char*)in->Name;void**s=(void**)&t->u1.Function;
if(g&& !strcmp(n2,"TextOutA"))ch|=Patch(s,o.ta,HTA);else if(g&&!strcmp(n2,"TextOutW"))ch|=Patch(s,o.tw,HTW);else if(g&&!strcmp(n2,"ExtTextOutA"))ch|=Patch(s,o.ea,HEA);else if(g&&!strcmp(n2,"ExtTextOutW"))ch|=Patch(s,o.ew,HEW);else if(u&&!strcmp(n2,"DrawTextA"))ch|=Patch(s,o.da,HDA);else if(u&&!strcmp(n2,"DrawTextW"))ch|=Patch(s,o.dw,HDW);else if(g&&!strcmp(n2,"GetGlyphOutlineA"))ch|=Patch(s,o.ga,HGA);else if(g&&!strcmp(n2,"GetGlyphOutlineW"))ch|=Patch(s,o.gw,HGW);else if(g&&!strcmp(n2,"CreateFontA"))ch|=Patch(s,o.cfa,HCFA);else if(g&&!strcmp(n2,"CreateFontW"))ch|=Patch(s,o.cfw,HCFW);else if(g&&!strcmp(n2,"CreateFontIndirectA"))ch|=Patch(s,o.cfia,HCFIA);else if(g&&!strcmp(n2,"CreateFontIndirectW"))ch|=Patch(s,o.cfiw,HCFIW);else if(g&&!strcmp(n2,"GetStockObject"))ch|=Patch(s,o.gso,HGSO);else if(g&&!strcmp(n2,"SelectObject"))ch|=Patch(s,o.so,HSO);else if(g&&!strcmp(n2,"GetDIBits"))ch|=Patch(s,o.gd,HGD);else if(g&&!strcmp(n2,"BitBlt"))ch|=Patch(s,o.bb,HBB);else if(g&&!strcmp(n2,"StretchBlt"))ch|=Patch(s,o.sb,HSB);else if(g&&!strcmp(n2,"PatBlt"))ch|=Patch(s,o.pb,HPB);else if(g&&!strcmp(n2,"AlphaBlend"))ch|=Patch(s,o.ab,HAB);else if(k&&!strcmp(n2,"GetProcAddress"))ch|=Patch(s,o.gpa,HGPA);else if(k&&!strcmp(n2,"LoadLibraryA"))ch|=Patch(s,o.lla,HLLA);else if(k&&!strcmp(n2,"LoadLibraryW"))ch|=Patch(s,o.llw,HLLW);}}return ch;}
void Scan(){HMODULE m[1024]{};DWORD bytes=0;if(!EnumProcessModules(GetCurrentProcess(),m,sizeof(m),&bytes))return;unsigned n=bytes/sizeof(HMODULE),ch=0;for(unsigned i=0;i<n;++i)if(PM(m[i]))++ch;Log(L"[IAT] modules="+std::to_wstring(n)+L" changed="+std::to_wstring(ch));}
DWORD WINAPI W(LPVOID){wchar_t b[MAX_PATH]{};DWORD n=GetTempPathW(MAX_PATH,b);logp=(n&&n<MAX_PATH)?std::wstring(b)+L"NOBU6HD_GDI_TRACE_V3.log":L"C:\\Temp\\NOBU6HD_GDI_TRACE_V3.log";DeleteFileW(logp.c_str());ready=true;Log(L"=== NOBU6HD GDI TRACE V3 START ===");Log(L"PID="+std::to_wstring(GetCurrentProcessId()));for(int i=0;i<120;i++){Scan();Sleep(500);}return 0;}
void Summary(){if(!ready)return;static const wchar_t*nm[]={L"TextOutA",L"TextOutW",L"ExtTextOutA",L"ExtTextOutW",L"DrawTextA",L"DrawTextW",L"GetGlyphOutlineA",L"GetGlyphOutlineW",L"CreateFontA",L"CreateFontW",L"CreateFontIndirectA",L"CreateFontIndirectW",L"GetStockObject",L"SelectObject",L"GetDIBits",L"BitBlt",L"StretchBlt",L"PatBlt",L"AlphaBlend",L"GetProcAddress",L"LoadLibraryA",L"LoadLibraryW"};Log(L"=== SUMMARY ===");for(int i=0;i<Count;i++)Log(std::wstring(nm[i])+L"="+std::to_wstring(c[i]));}
}
BOOL APIENTRY DllMain(HMODULE h,DWORD r,LPVOID){if(r==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,W,nullptr,0,nullptr);if(t)CloseHandle(t);}else if(r==DLL_PROCESS_DETACH)Summary();return TRUE;}
