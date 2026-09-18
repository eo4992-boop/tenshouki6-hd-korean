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

static std::wstring CallerInfo(DWORD eip) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>((UINT_PTR)eip), &m))
        return L"caller_module=<unknown> caller=0x" + std::to_wstring(eip);
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof(mi));
    UINT_PTR base = reinterpret_cast<UINT_PTR>(mi.lpBaseOfDll);
    UINT_PTR rva = eip >= base ? eip - base : 0;
    std::wstringstream s;
    s << L"caller_module=" << ModuleName(m)
      << L" caller=0x" << std::hex << std::uppercase << eip
      << L" caller_rva=0x" << rva;
    return s.str();
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* exe = L"NOBU6HD_JP.exe";
    std::wstring target = argc >= 2 ? argv[1] : L"織田信長";
    std::wofstream log("nobu_string_trace.txt", std::ios::out | std::ios::trunc);
    log.imbue(std::locale(log.getloc(), new std::codecvt_utf8<wchar_t>));

    std::wcout << L"NOBU6HD String Trace V1\n";
    std::wcout << L"Target: " << target << L"\n";

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p{sizeof(p)};
        if (Process32FirstW(snap, &p)) do {
            if (!_wcsicmp(p.szExeFile, exe)) { pid = p.th32ProcessID; break; }
        } while (Process32NextW(snap, &p));
        CloseHandle(snap);
    }
    if (!pid) {
        std::wcerr << L"ERROR: Start NOBU6HD_JP.exe first.\n";
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_OPERATION |
                                 PROCESS_VM_WRITE | PROCESS_CREATE_THREAD, FALSE, pid);
    if (!process) {
        std::wcerr << L"ERROR: OpenProcess failed: " << GetLastError() << L"\n";
        return 2;
    }

    Log(log, L"=== NOBU6HD STRING TRACE V1 START ===");
    Log(log, L"PID=" + std::to_wstring(pid));
    Log(log, L"TARGET=\"" + target + L"\"");

    auto hits = ScanProcess(process, target);
    std::wcout << L"Found " << hits.size() << L" candidate address(es).\n";
    Log(log, L"FOUND=" + std::to_wstring(hits.size()));
    for (const auto& h : hits) {
        std::wstringstream s;
        s << L"[STRING] encoding=" << h.encoding << L" address=0x"
          << std::hex << std::uppercase << h.address;
        Log(log, s.str());
        std::wcout << L"  " << Ansi932ToWide(h.encoding) << L" " << Hex(h.address) << L"\n";
    }

    if (hits.empty()) {
        Log(log, L"NO_MATCH: target is not present in readable process memory at scan time.");
        std::wcout << L"No match. Try a screen where the exact string is visible.\n";
        CloseHandle(process);
        return 3;
    }

    std::vector<UINT_PTR> bp;
    for (const auto& h : hits) {
        if (bp.size() >= 4) break;
        bp.push_back(h.address);
    }
    Log(log, L"[BREAKPOINTS] hardware_data_breakpoints=" + std::to_wstring(bp.size()));
    if (!DebugActiveProcess(pid)) {
        std::wcerr << L"ERROR: DebugActiveProcess failed: " << GetLastError() << L"\n";
        Log(log, L"ERROR: DebugActiveProcess failed=" + std::to_wstring(GetLastError()));
        CloseHandle(process);
        return 4;
    }
    DebugSetProcessKillOnExit(FALSE);

    bool configured = false;
    DEBUG_EVENT ev{};
    while (WaitForDebugEvent(&ev, INFINITE)) {
        DWORD continueStatus = DBG_CONTINUE;
        if (ev.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            HANDLE h = ev.u.CreateProcessInfo.hProcess;
            if (h) CloseHandle(h);
            configured = SetOnAllThreads(pid, bp);
            Log(log, L"[DEBUG] process attached; hardware breakpoints configured=" + std::to_wstring(configured ? 1 : 0));
        } else if (ev.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
            if (ev.u.CreateThread.hThread) {
                SetDataBreakpoints(ev.u.CreateThread.hThread, bp);
                CloseHandle(ev.u.CreateThread.hThread);
            }
        } else if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            auto& ex = ev.u.Exception.ExceptionRecord;
            if (ex.ExceptionCode == EXCEPTION_SINGLE_STEP) {
                HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ev.dwThreadId);
                DWORD eip = 0;
                DWORD dr6 = 0;
                if (t) {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(t, &c)) { eip = c.Eip; dr6 = c.Dr6; }
                    CloseHandle(t);
                }
                std::wstringstream s;
                s << L"[STRING ACCESS] thread=" << ev.dwThreadId
                  << L" dr6=0x" << std::hex << std::uppercase << dr6
                  << L" eip=0x" << eip << L" " << CallerInfo(eip);
                Log(log, s.str());
                std::wcout << s.str() << L"\n";
            } else if (ex.ExceptionCode == EXCEPTION_BREAKPOINT) {
                // Ignore the debugger's initial breakpoint.
            } else {
                continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            Log(log, L"=== PROCESS EXIT ===");
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
    }

    DebugActiveProcessStop(pid);
    CloseHandle(process);
    Log(log, L"=== NOBU6HD STRING TRACE V1 END ===");
    return 0;
}
