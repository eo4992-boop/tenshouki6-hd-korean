#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string>
#include <mutex>
#include <sstream>
#include <cstring>
#include <intrin.h>

#pragma comment(lib, "psapi.lib")

namespace {
std::mutex g_log_mutex;
std::wstring g_log_path;

enum Counter {
    TextOutA_Count, TextOutW_Count, ExtTextOutA_Count, ExtTextOutW_Count,
    DrawTextA_Count, DrawTextW_Count, GetGlyphOutlineA_Count, GetGlyphOutlineW_Count,
    CreateFontA_Count, CreateFontW_Count, CreateFontIndirectA_Count, CreateFontIndirectW_Count,
    GetStockObject_Count, SelectObject_Count, GetDIBits_Count,
    BitBlt_Count, StretchBlt_Count, PatBlt_Count, AlphaBlend_Count,
    GetProcAddress_Count, CounterCount
};
DWORD g_counts[CounterCount]{};

std::wstring GetLogPath() {
    wchar_t buffer[MAX_PATH]{};
    DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) return L"C:\\Temp\\nobu_gdi_trace.txt";
    return std::wstring(buffer) + L"nobu_gdi_trace.txt";
}

void Log(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    HANDLE file = CreateFileW(g_log_path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::wstring line = message + L"\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

std::wstring ToHex(UINT_PTR value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << value;
    return stream.str();
}

std::wstring AnsiToJapanese(const char* text, int length) {
    if (!text) return L"";
    if (length < 0) length = lstrlenA(text);
    if (length == 0) return L"";
    int required = MultiByteToWideChar(932, 0, text, length, nullptr, 0);
    if (required <= 0) return L"";
    std::wstring result(required, L'\0');
    MultiByteToWideChar(932, 0, text, length, result.data(), required);
    return result;
}

std::wstring WideText(const wchar_t* text, int length) {
    if (!text) return L"";
    if (length < 0) length = lstrlenW(text);
    return std::wstring(text, text + length);
}

void LogAnsiText(const wchar_t* api, const char* text, int length, Counter counter) {
    ++g_counts[counter];
    Log(L"[" + std::wstring(api) + L"] text=\"" + AnsiToJapanese(text, length) +
        L"\" len=" + std::to_wstring(length));
}

void LogWideText(const wchar_t* api, const wchar_t* text, int length, Counter counter) {
    ++g_counts[counter];
    Log(L"[" + std::wstring(api) + L"] text=\"" + WideText(text, length) +
        L"\" len=" + std::to_wstring(length));
}

using TextOutA_Fn = BOOL (WINAPI*)(HDC,int,int,LPCSTR,int);
using TextOutW_Fn = BOOL (WINAPI*)(HDC,int,int,LPCWSTR,int);
using ExtTextOutA_Fn = BOOL (WINAPI*)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*);
using ExtTextOutW_Fn = BOOL (WINAPI*)(HDC,int,int,UINT,const RECT*,LPCWSTR,UINT,const INT*);
using DrawTextA_Fn = int (WINAPI*)(HDC,LPCSTR,int,LPRECT,UINT);
using DrawTextW_Fn = int (WINAPI*)(HDC,LPCWSTR,int,LPRECT,UINT);
using GetGlyphOutlineA_Fn = DWORD (WINAPI*)(HDC,UINT,UINT,LPGLYPHMETRICS,DWORD,LPVOID,const MAT2*);
using GetGlyphOutlineW_Fn = DWORD (WINAPI*)(HDC,UINT,UINT,LPGLYPHMETRICS,DWORD,LPVOID,const MAT2*);
using CreateFontA_Fn = HFONT (WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR);
using CreateFontW_Fn = HFONT (WINAPI*)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCWSTR);
using CreateFontIndirectA_Fn = HFONT (WINAPI*)(const LOGFONTA*);
using CreateFontIndirectW_Fn = HFONT (WINAPI*)(const LOGFONTW*);
using GetStockObject_Fn = HGDIOBJ (WINAPI*)(int);
using SelectObject_Fn = HGDIOBJ (WINAPI*)(HDC,HGDIOBJ);
using GetDIBits_Fn = int (WINAPI*)(HDC,HBITMAP,UINT,UINT,LPVOID,const BITMAPINFO*,UINT);
using BitBlt_Fn = BOOL (WINAPI*)(HDC,int,int,int,int,HDC,int,int,DWORD);
using StretchBlt_Fn = BOOL (WINAPI*)(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);
using PatBlt_Fn = BOOL (WINAPI*)(HDC,int,int,int,int,DWORD);
using AlphaBlend_Fn = BOOL (WINAPI*)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
using GetProcAddress_Fn = FARPROC (WINAPI*)(HMODULE,LPCSTR);

struct Originals {
    TextOutA_Fn text_out_a{}; TextOutW_Fn text_out_w{};
    ExtTextOutA_Fn ext_text_out_a{}; ExtTextOutW_Fn ext_text_out_w{};
    DrawTextA_Fn draw_text_a{}; DrawTextW_Fn draw_text_w{};
    GetGlyphOutlineA_Fn glyph_a{}; GetGlyphOutlineW_Fn glyph_w{};
    CreateFontA_Fn create_font_a{}; CreateFontW_Fn create_font_w{};
    CreateFontIndirectA_Fn create_font_indirect_a{}; CreateFontIndirectW_Fn create_font_indirect_w{};
    GetStockObject_Fn get_stock_object{}; SelectObject_Fn select_object{}; GetDIBits_Fn get_dibits{};
    BitBlt_Fn bit_blt{}; StretchBlt_Fn stretch_blt{}; PatBlt_Fn pat_blt{}; AlphaBlend_Fn alpha_blend{};
    GetProcAddress_Fn get_proc_address{};
} g_originals;

BOOL WINAPI HookTextOutA(HDC h,int x,int y,LPCSTR t,int n){LogAnsiText(L"TextOutA",t,n,TextOutA_Count);return g_originals.text_out_a(h,x,y,t,n);}
BOOL WINAPI HookTextOutW(HDC h,int x,int y,LPCWSTR t,int n){LogWideText(L"TextOutW",t,n,TextOutW_Count);return g_originals.text_out_w(h,x,y,t,n);}
BOOL WINAPI HookExtTextOutA(HDC h,int x,int y,UINT o,const RECT*r,LPCSTR t,UINT n,const INT*d){LogAnsiText(L"ExtTextOutA",t,(int)n,ExtTextOutA_Count);return g_originals.ext_text_out_a(h,x,y,o,r,t,n,d);}
BOOL WINAPI HookExtTextOutW(HDC h,int x,int y,UINT o,const RECT*r,LPCWSTR t,UINT n,const INT*d){LogWideText(L"ExtTextOutW",t,(int)n,ExtTextOutW_Count);return g_originals.ext_text_out_w(h,x,y,o,r,t,n,d);}
int WINAPI HookDrawTextA(HDC h,LPCSTR t,int n,LPRECT r,UINT f){LogAnsiText(L"DrawTextA",t,n,DrawTextA_Count);return g_originals.draw_text_a(h,t,n,r,f);}
int WINAPI HookDrawTextW(HDC h,LPCWSTR t,int n,LPRECT r,UINT f){LogWideText(L"DrawTextW",t,n,DrawTextW_Count);return g_originals.draw_text_w(h,t,n,r,f);}
DWORD WINAPI HookGetGlyphOutlineA(HDC h,UINT c,UINT f,LPGLYPHMETRICS m,DWORD s,LPVOID b,const MAT2*a){++g_counts[GetGlyphOutlineA_Count];Log(L"[GetGlyphOutlineA] char="+ToHex(c)+L" flags="+ToHex(f));return g_originals.glyph_a(h,c,f,m,s,b,a);}
DWORD WINAPI HookGetGlyphOutlineW(HDC h,UINT c,UINT f,LPGLYPHMETRICS m,DWORD s,LPVOID b,const MAT2*a){++g_counts[GetGlyphOutlineW_Count];Log(L"[GetGlyphOutlineW] char="+ToHex(c)+L" flags="+ToHex(f));return g_originals.glyph_w(h,c,f,m,s,b,a);}
HFONT WINAPI HookCreateFontA(int a,int b,int c,int d,int e,DWORD f,DWORD g,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,LPCSTR face){++g_counts[CreateFontA_Count];Log(L"[CreateFontA] face=\""+AnsiToJapanese(face,-1)+L"\"");return g_originals.create_font_a(a,b,c,d,e,f,g,h,i,j,k,l,m,face);}
HFONT WINAPI HookCreateFontW(int a,int b,int c,int d,int e,DWORD f,DWORD g,DWORD h,DWORD i,DWORD j,DWORD k,DWORD l,DWORD m,LPCWSTR face){++g_counts[CreateFontW_Count];Log(L"[CreateFontW] face=\""+std::wstring(face?face:L"")+L"\"");return g_originals.create_font_w(a,b,c,d,e,f,g,h,i,j,k,l,m,face);}
HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA*l){++g_counts[CreateFontIndirectA_Count];Log(L"[CreateFontIndirectA]");return g_originals.create_font_indirect_a(l);}
HFONT WINAPI HookCreateFontIndirectW(const LOGFONTW*l){++g_counts[CreateFontIndirectW_Count];Log(L"[CreateFontIndirectW]");return g_originals.create_font_indirect_w(l);}
HGDIOBJ WINAPI HookGetStockObject(int o){++g_counts[GetStockObject_Count];if(o==17)Log(L"[GetStockObject] type=17 (DEFAULT_GUI_FONT)");return g_originals.get_stock_object(o);}
HGDIOBJ WINAPI HookSelectObject(HDC h,HGDIOBJ o){++g_counts[SelectObject_Count];return g_originals.select_object(h,o);}
int WINAPI HookGetDIBits(HDC h,HBITMAP b,UINT s,UINT n,LPVOID p,const BITMAPINFO*i,UINT u){++g_counts[GetDIBits_Count];Log(L"[GetDIBits] start="+std::to_wstring(s)+L" lines="+std::to_wstring(n));return g_originals.get_dibits(h,b,s,n,p,i,u);}
BOOL WINAPI HookBitBlt(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,DWORD rop){++g_counts[BitBlt_Count];return g_originals.bit_blt(a,x,y,w,h,b,sx,sy,rop);}
BOOL WINAPI HookStretchBlt(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,int sw,int sh,DWORD rop){++g_counts[StretchBlt_Count];return g_originals.stretch_blt(a,x,y,w,h,b,sx,sy,sw,sh,rop);}
BOOL WINAPI HookPatBlt(HDC h,int x,int y,int w,int d,DWORD rop){++g_counts[PatBlt_Count];return g_originals.pat_blt(h,x,y,w,d,rop);}
BOOL WINAPI HookAlphaBlend(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,int sw,int sh,BLENDFUNCTION f){++g_counts[AlphaBlend_Count];return g_originals.alpha_blend(a,x,y,w,h,b,sx,sy,sw,sh,f);}
std::wstring ModuleName(HMODULE module){
    if(!module)return L"<null>";
    wchar_t path[MAX_PATH]{};
    DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
    if(length==0)return L"<unknown>";
    std::wstring value(path,length);
    size_t slash=value.find_last_of(L"\\\\/");
    return slash==std::wstring::npos?value:value.substr(slash+1);
}

struct CallSiteInfo {
    HMODULE module{};
    UINT_PTR address{};
    UINT_PTR rva{};
};

CallSiteInfo GetCallSite() {
    CallSiteInfo info{};
    info.address = reinterpret_cast<UINT_PTR>(_ReturnAddress());
    HMODULE module = nullptr;
    if (info.address &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(info.address), &module)) {
        info.module = module;
        BYTE* base = reinterpret_cast<BYTE*>(module);
        if (base && info.address >= reinterpret_cast<UINT_PTR>(base))
            info.rva = info.address - reinterpret_cast<UINT_PTR>(base);
    }
    return info;
}

thread_local bool g_in_getproc_hook = false;

FARPROC WINAPI HookGetProcAddress(HMODULE m,LPCSTR name){
    ++g_counts[GetProcAddress_Count];

    if (g_in_getproc_hook)
        return g_originals.get_proc_address(m,name);

    g_in_getproc_hook = true;
    FARPROC result = g_originals.get_proc_address(m,name);

    std::wstring requested;
    if(!name){
        requested=L"<null>";
    }else if(HIWORD(name)==0){
        requested=L"#"+std::to_wstring(LOWORD(name));
    }else{
        requested=AnsiToJapanese(name,-1);
    }

    CallSiteInfo caller = GetCallSite();
    Log(L"[GetProcAddress] module="+ModuleName(m)+
        L" requested=\"" + requested +
        L"\" result=" + ToHex(reinterpret_cast<UINT_PTR>(result)) +
        L" caller_module=" + ModuleName(caller.module) +
        L" caller=" + ToHex(caller.address) +
        L" caller_rva=" + ToHex(caller.rva));

    g_in_getproc_hook = false;
    return result;
}

template <typename Function>
bool PatchSlot(void** slot,Function& original,Function replacement){
    Function* typed_slot=reinterpret_cast<Function*>(slot);
    if(*typed_slot==replacement)return false;
    if(!original)original=*typed_slot;
    DWORD old=0;
    if(!VirtualProtect(typed_slot,sizeof(Function),PAGE_READWRITE,&old))return false;
    *typed_slot=replacement;
    DWORD ignored=0; VirtualProtect(typed_slot,sizeof(Function),old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),typed_slot,sizeof(Function));
    return true;
}

bool PatchModule(HMODULE module){
    BYTE* base=reinterpret_cast<BYTE*>(module);
    auto* dos=reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if(!dos||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto* nt=reinterpret_cast<PIMAGE_NT_HEADERS>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    const auto& imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!imports.VirtualAddress)return false;
    auto* descriptor=reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base+imports.VirtualAddress);
    bool changed=false;
    for(;descriptor->Name;++descriptor){
        const char* dll=reinterpret_cast<const char*>(base+descriptor->Name);
        bool gdi=_stricmp(dll,"gdi32.dll")==0;
        bool user=_stricmp(dll,"user32.dll")==0;
        bool kernel=_stricmp(dll,"kernel32.dll")==0;
        if(!gdi&&!user&&!kernel)continue;
        if(!descriptor->OriginalFirstThunk)continue;
        auto* thunk=reinterpret_cast<PIMAGE_THUNK_DATA>(base+descriptor->FirstThunk);
        auto* orig=reinterpret_cast<PIMAGE_THUNK_DATA>(base+descriptor->OriginalFirstThunk);
        for(;orig->u1.AddressOfData;++orig,++thunk){
            if(IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))continue;
            auto* import_name=reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base+orig->u1.AddressOfData);
            const char* name=reinterpret_cast<const char*>(import_name->Name);
            void** slot=reinterpret_cast<void**>(&thunk->u1.Function);
            if(gdi&&std::strcmp(name,"TextOutA")==0)changed|=PatchSlot(slot,g_originals.text_out_a,HookTextOutA);
            else if(gdi&&std::strcmp(name,"TextOutW")==0)changed|=PatchSlot(slot,g_originals.text_out_w,HookTextOutW);
            else if(gdi&&std::strcmp(name,"ExtTextOutA")==0)changed|=PatchSlot(slot,g_originals.ext_text_out_a,HookExtTextOutA);
            else if(gdi&&std::strcmp(name,"ExtTextOutW")==0)changed|=PatchSlot(slot,g_originals.ext_text_out_w,HookExtTextOutW);
            else if(user&&std::strcmp(name,"DrawTextA")==0)changed|=PatchSlot(slot,g_originals.draw_text_a,HookDrawTextA);
            else if(user&&std::strcmp(name,"DrawTextW")==0)changed|=PatchSlot(slot,g_originals.draw_text_w,HookDrawTextW);
            else if(gdi&&std::strcmp(name,"GetGlyphOutlineA")==0)changed|=PatchSlot(slot,g_originals.glyph_a,HookGetGlyphOutlineA);
            else if(gdi&&std::strcmp(name,"GetGlyphOutlineW")==0)changed|=PatchSlot(slot,g_originals.glyph_w,HookGetGlyphOutlineW);
            else if(gdi&&std::strcmp(name,"CreateFontA")==0)changed|=PatchSlot(slot,g_originals.create_font_a,HookCreateFontA);
            else if(gdi&&std::strcmp(name,"CreateFontW")==0)changed|=PatchSlot(slot,g_originals.create_font_w,HookCreateFontW);
            else if(gdi&&std::strcmp(name,"CreateFontIndirectA")==0)changed|=PatchSlot(slot,g_originals.create_font_indirect_a,HookCreateFontIndirectA);
            else if(gdi&&std::strcmp(name,"CreateFontIndirectW")==0)changed|=PatchSlot(slot,g_originals.create_font_indirect_w,HookCreateFontIndirectW);
            else if(gdi&&std::strcmp(name,"GetStockObject")==0)changed|=PatchSlot(slot,g_originals.get_stock_object,HookGetStockObject);
            else if(gdi&&std::strcmp(name,"SelectObject")==0)changed|=PatchSlot(slot,g_originals.select_object,HookSelectObject);
            else if(gdi&&std::strcmp(name,"GetDIBits")==0)changed|=PatchSlot(slot,g_originals.get_dibits,HookGetDIBits);
            else if(gdi&&std::strcmp(name,"BitBlt")==0)changed|=PatchSlot(slot,g_originals.bit_blt,HookBitBlt);
            else if(gdi&&std::strcmp(name,"StretchBlt")==0)changed|=PatchSlot(slot,g_originals.stretch_blt,HookStretchBlt);
            else if(gdi&&std::strcmp(name,"PatBlt")==0)changed|=PatchSlot(slot,g_originals.pat_blt,HookPatBlt);
            else if(gdi&&std::strcmp(name,"AlphaBlend")==0)changed|=PatchSlot(slot,g_originals.alpha_blend,HookAlphaBlend);
            else if(kernel&&std::strcmp(name,"GetProcAddress")==0)changed|=PatchSlot(slot,g_originals.get_proc_address,HookGetProcAddress);
        }
    }
    return changed;
}

DWORD WINAPI TraceWorker(LPVOID){
    g_log_path=GetLogPath(); DeleteFileW(g_log_path.c_str());
    Log(L"=== NOBU6HD GDI TRACE V3 START ==="); Log(L"PID="+std::to_wstring(GetCurrentProcessId()));
    HMODULE modules[1024]{}; DWORD bytes=0,changed=0;
    if(EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)){
        unsigned count=bytes/sizeof(HMODULE);
        for(unsigned i=0;i<count;++i)if(PatchModule(modules[i]))++changed;
    }
    Log(L"[IAT] modules_changed="+std::to_wstring(changed));
    return 0;
}

void WriteSummary(){
    if(g_log_path.empty())return;
    static const wchar_t* names[CounterCount]={
        L"TextOutA",L"TextOutW",L"ExtTextOutA",L"ExtTextOutW",L"DrawTextA",L"DrawTextW",
        L"GetGlyphOutlineA",L"GetGlyphOutlineW",L"CreateFontA",L"CreateFontW",
        L"CreateFontIndirectA",L"CreateFontIndirectW",L"GetStockObject",L"SelectObject",
        L"GetDIBits",L"BitBlt",L"StretchBlt",L"PatBlt",L"AlphaBlend",L"GetProcAddress"
    };
    Log(L"=== SUMMARY ===");
    for(int i=0;i<CounterCount;++i)Log(std::wstring(names[i])+L"="+std::to_wstring(g_counts[i]));
}

}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,TraceWorker,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }else if(reason==DLL_PROCESS_DETACH)WriteSummary();
    return TRUE;
}
