#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string>
#include <mutex>
#include <sstream>
#include <cstring>

#pragma comment(lib, "psapi.lib")

namespace {

std::mutex g_log_mutex;
std::wstring g_log_path;

enum Counter {
    TextOutA_Count,
    TextOutW_Count,
    ExtTextOutA_Count,
    ExtTextOutW_Count,
    DrawTextA_Count,
    DrawTextW_Count,
    GetGlyphOutlineA_Count,
    GetGlyphOutlineW_Count,
    CreateFontA_Count,
    CreateFontW_Count,
    CreateFontIndirectA_Count,
    CreateFontIndirectW_Count,
    GetStockObject_Count,
    SelectObject_Count,
    GetDIBits_Count,
    CounterCount
};

DWORD g_counts[CounterCount]{};

std::wstring GetLogPath()
{
    wchar_t buffer[MAX_PATH]{};
    DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH)
        return L"C:\\Temp\\nobu_gdi_trace.txt";
    return std::wstring(buffer) + L"nobu_gdi_trace.txt";
}

void Log(const std::wstring& message)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);

    HANDLE file = CreateFileW(
        g_log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return;

    std::wstring line = message + L"\r\n";
    DWORD written = 0;
    WriteFile(
        file,
        line.data(),
        static_cast<DWORD>(line.size() * sizeof(wchar_t)),
        &written,
        nullptr);
    CloseHandle(file);
}

std::wstring ToHex(UINT_PTR value)
{
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << value;
    return stream.str();
}

std::wstring AnsiToJapanese(const char* text, int length)
{
    if (!text)
        return L"";

    if (length < 0)
        length = lstrlenA(text);

    if (length == 0)
        return L"";

    int required = MultiByteToWideChar(932, 0, text, length, nullptr, 0);
    if (required <= 0)
        return L"";

    std::wstring result(required, L'\0');
    MultiByteToWideChar(932, 0, text, length, result.data(), required);
    return result;
}

std::wstring WideText(const wchar_t* text, int length)
{
    if (!text)
        return L"";

    if (length < 0)
        length = lstrlenW(text);

    return std::wstring(text, text + length);
}

void LogAnsiText(const wchar_t* api, const char* text, int length, Counter counter)
{
    ++g_counts[counter];
    Log(
        L"[" + std::wstring(api) +
        L"] text=\"" + AnsiToJapanese(text, length) +
        L"\" len=" + std::to_wstring(length));
}

void LogWideText(const wchar_t* api, const wchar_t* text, int length, Counter counter)
{
    ++g_counts[counter];
    Log(
        L"[" + std::wstring(api) +
        L"] text=\"" + WideText(text, length) +
        L"\" len=" + std::to_wstring(length));
}

using TextOutA_Fn = BOOL (WINAPI*)(HDC, int, int, LPCSTR, int);
using TextOutW_Fn = BOOL (WINAPI*)(HDC, int, int, LPCWSTR, int);

using ExtTextOutA_Fn = BOOL (WINAPI*)(
    HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const INT*);
using ExtTextOutW_Fn = BOOL (WINAPI*)(
    HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const INT*);

using DrawTextA_Fn = int (WINAPI*)(HDC, LPCSTR, int, LPRECT, UINT);
using DrawTextW_Fn = int (WINAPI*)(HDC, LPCWSTR, int, LPRECT, UINT);

using GetGlyphOutlineA_Fn = DWORD (WINAPI*)(
    HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
using GetGlyphOutlineW_Fn = DWORD (WINAPI*)(
    HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);

using CreateFontA_Fn = HFONT (WINAPI*)(
    int, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
    DWORD, DWORD, DWORD, DWORD, LPCSTR);
using CreateFontW_Fn = HFONT (WINAPI*)(
    int, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
    DWORD, DWORD, DWORD, DWORD, LPCWSTR);

using CreateFontIndirectA_Fn = HFONT (WINAPI*)(const LOGFONTA*);
using CreateFontIndirectW_Fn = HFONT (WINAPI*)(const LOGFONTW*);

using GetStockObject_Fn = HGDIOBJ (WINAPI*)(int);
using SelectObject_Fn = HGDIOBJ (WINAPI*)(HDC, HGDIOBJ);

using GetDIBits_Fn = int (WINAPI*)(
    HDC, HBITMAP, UINT, UINT, LPVOID, const BITMAPINFO*, UINT);

struct Originals
{
    TextOutA_Fn text_out_a{};
    TextOutW_Fn text_out_w{};
    ExtTextOutA_Fn ext_text_out_a{};
    ExtTextOutW_Fn ext_text_out_w{};
    DrawTextA_Fn draw_text_a{};
    DrawTextW_Fn draw_text_w{};
    GetGlyphOutlineA_Fn glyph_a{};
    GetGlyphOutlineW_Fn glyph_w{};
    CreateFontA_Fn create_font_a{};
    CreateFontW_Fn create_font_w{};
    CreateFontIndirectA_Fn create_font_indirect_a{};
    CreateFontIndirectW_Fn create_font_indirect_w{};
    GetStockObject_Fn get_stock_object{};
    SelectObject_Fn select_object{};
    GetDIBits_Fn get_dibits{};
} g_originals;

BOOL WINAPI HookTextOutA(HDC hdc, int x, int y, LPCSTR text, int length)
{
    LogAnsiText(L"TextOutA", text, length, TextOutA_Count);
    return g_originals.text_out_a(hdc, x, y, text, length);
}

BOOL WINAPI HookTextOutW(HDC hdc, int x, int y, LPCWSTR text, int length)
{
    LogWideText(L"TextOutW", text, length, TextOutW_Count);
    return g_originals.text_out_w(hdc, x, y, text, length);
}

BOOL WINAPI HookExtTextOutA(
    HDC hdc, int x, int y, UINT options, const RECT* rect,
    LPCSTR text, UINT length, const INT* dx)
{
    LogAnsiText(L"ExtTextOutA", text, static_cast<int>(length), ExtTextOutA_Count);
    return g_originals.ext_text_out_a(
        hdc, x, y, options, rect, text, length, dx);
}

BOOL WINAPI HookExtTextOutW(
    HDC hdc, int x, int y, UINT options, const RECT* rect,
    LPCWSTR text, UINT length, const INT* dx)
{
    LogWideText(L"ExtTextOutW", text, static_cast<int>(length), ExtTextOutW_Count);
    return g_originals.ext_text_out_w(
        hdc, x, y, options, rect, text, length, dx);
}

int WINAPI HookDrawTextA(
    HDC hdc, LPCSTR text, int length, LPRECT rect, UINT format)
{
    LogAnsiText(L"DrawTextA", text, length, DrawTextA_Count);
    return g_originals.draw_text_a(hdc, text, length, rect, format);
}

int WINAPI HookDrawTextW(
    HDC hdc, LPCWSTR text, int length, LPRECT rect, UINT format)
{
    LogWideText(L"DrawTextW", text, length, DrawTextW_Count);
    return g_originals.draw_text_w(hdc, text, length, rect, format);
}

DWORD WINAPI HookGetGlyphOutlineA(
    HDC hdc, UINT character, UINT format, LPGLYPHMETRICS metrics,
    DWORD buffer_size, LPVOID buffer, const MAT2* matrix)
{
    ++g_counts[GetGlyphOutlineA_Count];
    Log(
        L"[GetGlyphOutlineA] char=" + ToHex(character) +
        L" flags=" + ToHex(format));
    return g_originals.glyph_a(
        hdc, character, format, metrics, buffer_size, buffer, matrix);
}

DWORD WINAPI HookGetGlyphOutlineW(
    HDC hdc, UINT character, UINT format, LPGLYPHMETRICS metrics,
    DWORD buffer_size, LPVOID buffer, const MAT2* matrix)
{
    ++g_counts[GetGlyphOutlineW_Count];
    Log(
        L"[GetGlyphOutlineW] char=" + ToHex(character) +
        L" flags=" + ToHex(format));
    return g_originals.glyph_w(
        hdc, character, format, metrics, buffer_size, buffer, matrix);
}

HFONT WINAPI HookCreateFontA(
    int a, int b, int c, int d, int e,
    DWORD f, DWORD g, DWORD h, DWORD i, DWORD j,
    DWORD k, DWORD l, DWORD m, LPCSTR face)
{
    ++g_counts[CreateFontA_Count];
    Log(L"[CreateFontA] face=\"" + AnsiToJapanese(face, -1) + L"\"");
    return g_originals.create_font_a(
        a, b, c, d, e, f, g, h, i, j, k, l, m, face);
}

HFONT WINAPI HookCreateFontW(
    int a, int b, int c, int d, int e,
    DWORD f, DWORD g, DWORD h, DWORD i, DWORD j,
    DWORD k, DWORD l, DWORD m, LPCWSTR face)
{
    ++g_counts[CreateFontW_Count];
    Log(L"[CreateFontW] face=\"" + std::wstring(face ? face : L"") + L"\"");
    return g_originals.create_font_w(
        a, b, c, d, e, f, g, h, i, j, k, l, m, face);
}

HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA* logfont)
{
    ++g_counts[CreateFontIndirectA_Count];
    Log(L"[CreateFontIndirectA]");
    return g_originals.create_font_indirect_a(logfont);
}

HFONT WINAPI HookCreateFontIndirectW(const LOGFONTW* logfont)
{
    ++g_counts[CreateFontIndirectW_Count];
    Log(L"[CreateFontIndirectW]");
    return g_originals.create_font_indirect_w(logfont);
}

HGDIOBJ WINAPI HookGetStockObject(int object)
{
    ++g_counts[GetStockObject_Count];
    if (object == 17)
        Log(L"[GetStockObject] type=17");
    return g_originals.get_stock_object(object);
}

HGDIOBJ WINAPI HookSelectObject(HDC hdc, HGDIOBJ object)
{
    ++g_counts[SelectObject_Count];
    return g_originals.select_object(hdc, object);
}

int WINAPI HookGetDIBits(
    HDC hdc, HBITMAP bitmap, UINT start_scan, UINT scan_lines,
    LPVOID bits, const BITMAPINFO* info, UINT usage)
{
    ++g_counts[GetDIBits_Count];
    Log(
        L"[GetDIBits] start=" + std::to_wstring(start_scan) +
        L" lines=" + std::to_wstring(scan_lines));
    return g_originals.get_dibits(
        hdc, bitmap, start_scan, scan_lines, bits, info, usage);
}

template <typename Function>
bool PatchSlot(void** slot, Function& original, Function replacement)
{
    Function* typed_slot = reinterpret_cast<Function*>(slot);

    if (*typed_slot == replacement)
        return false;

    if (!original)
        original = *typed_slot;

    DWORD old_protection = 0;
    if (!VirtualProtect(
            typed_slot, sizeof(Function), PAGE_READWRITE, &old_protection))
        return false;

    *typed_slot = replacement;

    DWORD ignored = 0;
    VirtualProtect(
        typed_slot, sizeof(Function), old_protection, &ignored);
    FlushInstructionCache(
        GetCurrentProcess(), typed_slot, sizeof(Function));

    return true;
}

bool PatchModule(HMODULE module)
{
    BYTE* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);

    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto& imports =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (!imports.VirtualAddress)
        return false;

    auto* descriptor =
        reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            base + imports.VirtualAddress);

    bool changed = false;

    for (; descriptor->Name; ++descriptor)
    {
        const char* dll_name =
            reinterpret_cast<const char*>(base + descriptor->Name);

        if (_stricmp(dll_name, "gdi32.dll") != 0 &&
            _stricmp(dll_name, "user32.dll") != 0)
            continue;

        if (!descriptor->OriginalFirstThunk)
            continue;

        auto* thunk =
            reinterpret_cast<PIMAGE_THUNK_DATA>(
                base + descriptor->FirstThunk);
        auto* original_thunk =
            reinterpret_cast<PIMAGE_THUNK_DATA>(
                base + descriptor->OriginalFirstThunk);

        for (; original_thunk->u1.AddressOfData;
             ++original_thunk, ++thunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal))
                continue;

            auto* import_name =
                reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                    base + original_thunk->u1.AddressOfData);

            const char* name =
                reinterpret_cast<const char*>(import_name->Name);

            void** slot =
                reinterpret_cast<void**>(&thunk->u1.Function);

            if (std::strcmp(name, "TextOutA") == 0)
                changed |= PatchSlot(slot, g_originals.text_out_a, HookTextOutA);
            else if (std::strcmp(name, "TextOutW") == 0)
                changed |= PatchSlot(slot, g_originals.text_out_w, HookTextOutW);
            else if (std::strcmp(name, "ExtTextOutA") == 0)
                changed |= PatchSlot(slot, g_originals.ext_text_out_a, HookExtTextOutA);
            else if (std::strcmp(name, "ExtTextOutW") == 0)
                changed |= PatchSlot(slot, g_originals.ext_text_out_w, HookExtTextOutW);
            else if (std::strcmp(name, "DrawTextA") == 0)
                changed |= PatchSlot(slot, g_originals.draw_text_a, HookDrawTextA);
            else if (std::strcmp(name, "DrawTextW") == 0)
                changed |= PatchSlot(slot, g_originals.draw_text_w, HookDrawTextW);
            else if (std::strcmp(name, "GetGlyphOutlineA") == 0)
                changed |= PatchSlot(slot, g_originals.glyph_a, HookGetGlyphOutlineA);
            else if (std::strcmp(name, "GetGlyphOutlineW") == 0)
                changed |= PatchSlot(slot, g_originals.glyph_w, HookGetGlyphOutlineW);
            else if (std::strcmp(name, "CreateFontA") == 0)
                changed |= PatchSlot(slot, g_originals.create_font_a, HookCreateFontA);
            else if (std::strcmp(name, "CreateFontW") == 0)
                changed |= PatchSlot(slot, g_originals.create_font_w, HookCreateFontW);
            else if (std::strcmp(name, "CreateFontIndirectA") == 0)
                changed |= PatchSlot(
                    slot, g_originals.create_font_indirect_a, HookCreateFontIndirectA);
            else if (std::strcmp(name, "CreateFontIndirectW") == 0)
                changed |= PatchSlot(
                    slot, g_originals.create_font_indirect_w, HookCreateFontIndirectW);
            else if (std::strcmp(name, "GetStockObject") == 0)
                changed |= PatchSlot(slot, g_originals.get_stock_object, HookGetStockObject);
            else if (std::strcmp(name, "SelectObject") == 0)
                changed |= PatchSlot(slot, g_originals.select_object, HookSelectObject);
            else if (std::strcmp(name, "GetDIBits") == 0)
                changed |= PatchSlot(slot, g_originals.get_dibits, HookGetDIBits);
        }
    }

    return changed;
}

DWORD WINAPI TraceWorker(LPVOID)
{
    g_log_path = GetLogPath();
    DeleteFileW(g_log_path.c_str());

    Log(L"=== NOBU6HD GDI TRACE START ===");
    Log(L"PID=" + std::to_wstring(GetCurrentProcessId()));

    HMODULE modules[1024]{};
    DWORD bytes_needed = 0;
    DWORD changed_modules = 0;

    if (EnumProcessModules(
            GetCurrentProcess(),
            modules,
            sizeof(modules),
            &bytes_needed))
    {
        unsigned count = bytes_needed / sizeof(HMODULE);

        for (unsigned i = 0; i < count; ++i)
        {
            if (PatchModule(modules[i]))
                ++changed_modules;
        }
    }

    Log(
        L"[IAT] modules_changed=" +
        std::to_wstring(changed_modules));

    return 0;
}

void WriteSummary()
{
    if (g_log_path.empty())
        return;

    static const wchar_t* names[CounterCount] = {
        L"TextOutA",
        L"TextOutW",
        L"ExtTextOutA",
        L"ExtTextOutW",
        L"DrawTextA",
        L"DrawTextW",
        L"GetGlyphOutlineA",
        L"GetGlyphOutlineW",
        L"CreateFontA",
        L"CreateFontW",
        L"CreateFontIndirectA",
        L"CreateFontIndirectW",
        L"GetStockObject",
        L"SelectObject",
        L"GetDIBits"
    };

    Log(L"=== SUMMARY ===");

    for (int i = 0; i < CounterCount; ++i)
        Log(
            std::wstring(names[i]) +
            L"=" + std::to_wstring(g_counts[i]));
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(
            nullptr, 0, TraceWorker, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        WriteSummary();
    }

    return TRUE;
}
