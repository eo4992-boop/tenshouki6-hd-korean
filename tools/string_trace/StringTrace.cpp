#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <codecvt>

#pragma comment(lib, "psapi.lib")

struct Hit {
    UINT_PTR address{};
    std::string encoding;
};

static std::string Hex(UINT_PTR v) {
    std::ostringstream s;
    s << "0x" << std::hex << std::uppercase << v;
    return s.str();
}

static std::wstring ModuleName(HMODULE h) {
    if (!h) return L"<unknown>";
    wchar_t path[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(h, path, MAX_PATH);
    if (!n) return L"<unknown>";
    std::wstring p(path, n);
    size_t x = p.find_last_of(L"\\/");
    return x == std::wstring::npos ? p : p.substr(x + 1);
}

static std::wstring Ansi932ToWide(const std::string& s) {
    int n = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);
    if (!n) return L"";
    std::wstring out(n, L'\\0');
    MultiByteToWideChar(932, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

static std::vector<BYTE> WideTo932(const std::wstring& s) {
    int n = WideCharToMultiByte(932, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::vector<BYTE> out(n);
    WideCharToMultiByte(932, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static void Log(std::wofstream& f, const std::wstring& s) {
    f << s << L"\n";
    f.flush();
}

static std::vector<Hit> ScanProcess(HANDLE process, const std::wstring& target) {
    std::vector<Hit> hits;
    std::vector<BYTE> cp = WideTo932(target);
    std::vector<BYTE> utf16(target.size() * sizeof(wchar_t));
    if (!utf16.empty()) memcpy(utf16.data(), target.data(), utf16.size());

    const SIZE_T chunk = 1024 * 1024;
    std::vector<BYTE> buf(chunk + 4096);
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    UINT_PTR addr = reinterpret_cast<UINT_PTR>(si.lpMinimumApplicationAddress);
    UINT_PTR max = reinterpret_cast<UINT_PTR>(si.lpMaximumApplicationAddress);

    while (addr < max) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) break;
        UINT_PTR base = reinterpret_cast<UINT_PTR>(mbi.BaseAddress);
        UINT_PTR end = base + mbi.RegionSize;
        bool readable = mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_NOACCESS) &&
            !(mbi.Protect & PAGE_GUARD);
        if (readable && mbi.RegionSize >= 1) {
            for (UINT_PTR pos = base; pos < end; ) {
                SIZE_T want = (SIZE_T)std::min<UINT_PTR>(chunk, end - pos);
                SIZE_T got = 0;
                if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(pos), buf.data(), want, &got) && got) {
                    auto find_bytes = [&](const std::vector<BYTE>& needle, const char* enc) {
                        if (needle.empty() || got < needle.size()) return;
                        for (SIZE_T i = 0; i + needle.size() <= got; ++i) {
                            if (memcmp(buf.data() + i, needle.data(), needle.size()) == 0) {
                                UINT_PTR a = pos + i;
                                if (std::none_of(hits.begin(), hits.end(), [&](const Hit& h){ return h.address == a; }))
                                    hits.push_back({a, enc});
                            }
                        }
                    };
                    find_bytes(cp, "CP932");
                    find_bytes(utf16, "UTF16");
                    if (hits.size() >= 32) return hits;
                }
                if (got == 0) break;
                pos += got;
            }
        }
        if (end <= addr) break;
        addr = end;
    }
    return hits;
}

static bool SetDataBreakpoints(HANDLE thread, const std::vector<UINT_PTR>& addresses) {
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &c)) return false;
    c.Dr0 = c.Dr1 = c.Dr2 = c.Dr3 = 0;
    c.Dr6 = 0;
    c.Dr7 = 0;
    for (size_t i = 0; i < addresses.size() && i < 4; ++i) {
        (&c.Dr0)[i] = addresses[i];
        c.Dr7 |= (1u << (i * 2));                 // local enable
        c.Dr7 |= (3u << (16 + i * 4));             // RW=11 read/write
    }
    return SetThreadContext(thread, &c) != FALSE;
}

static bool SetOnAllThreads(DWORD pid, const std::vector<UINT_PTR>& addresses) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te{sizeof(te)};
    bool ok = true;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) { ok = false; continue; }
            if (!SetDataBreakpoints(t, addresses)) ok = false;
            CloseHandle(t);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return ok;
}

static std::wstring CallerInfo(DWORD pid, DWORD eip) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return L"caller_module=<unknown>";
    MODULEENTRY32W me{sizeof(me)};
    std::wstring result = L"caller_module=<unknown>";
    if (Module32FirstW(snap, &me)) {
        do {
            UINT_PTR base = reinterpret_cast<UINT_PTR>(me.modBaseAddr);
            UINT_PTR end = base + me.modBaseSize;
            if ((UINT_PTR)eip >= base && (UINT_PTR)eip < end) {
                std::wstringstream s;
                s << L"caller_module=" << me.szModule
                  << L" caller=0x" << std::hex << std::uppercase << eip
                  << L" caller_rva=0x" << ((UINT_PTR)eip - base);
                result = s.str();
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return result;
}
