#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstring>

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

static std::wstring ModuleNameFromAddress(DWORD pid, UINT_PTR address) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return L"<unknown>";
    MODULEENTRY32W me{sizeof(me)};
    std::wstring result = L"<unknown>";
    if (Module32FirstW(snap, &me)) {
        do {
            UINT_PTR base = reinterpret_cast<UINT_PTR>(me.modBaseAddr);
            UINT_PTR end = base + me.modBaseSize;
            if (address >= base && address < end) {
                std::wstringstream s;
                s << me.szModule
                  << L" caller=0x" << std::hex << std::uppercase << address
                  << L" caller_rva=0x" << (address - base);
                result = s.str();
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return result;
}

static std::vector<BYTE> WideTo932(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(932, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::vector<BYTE> out(static_cast<size_t>(n));
    WideCharToMultiByte(932, 0, s.c_str(), static_cast<int>(s.size()),
                        reinterpret_cast<LPSTR>(out.data()), n, nullptr, nullptr);
    return out;
}

static std::vector<BYTE> WideToUtf16Bytes(const std::wstring& s) {
    std::vector<BYTE> out(s.size() * sizeof(wchar_t));
    if (!out.empty()) {
        memcpy(out.data(), s.data(), out.size());
    }
    return out;
}

static std::vector<Hit> ScanProcess(HANDLE process, const std::wstring& target) {
    std::vector<Hit> hits;
    const std::vector<BYTE> cp932 = WideTo932(target);
    const std::vector<BYTE> utf16 = WideToUtf16Bytes(target);
    const SIZE_T chunk = 1024 * 1024;
    std::vector<BYTE> buffer(chunk);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    UINT_PTR address = reinterpret_cast<UINT_PTR>(si.lpMinimumApplicationAddress);
    const UINT_PTR maximum = reinterpret_cast<UINT_PTR>(si.lpMaximumApplicationAddress);

    while (address < maximum) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) break;

        UINT_PTR base = reinterpret_cast<UINT_PTR>(mbi.BaseAddress);
        UINT_PTR end = base + mbi.RegionSize;
        if (end <= address) break;

        const DWORD protect = mbi.Protect & 0xFF;
        const bool readable =
            mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_GUARD) &&
            protect != PAGE_NOACCESS;

        if (readable) {
            for (UINT_PTR pos = base; pos < end; ) {
                SIZE_T want = static_cast<SIZE_T>(std::min<UINT_PTR>(chunk, end - pos));
                SIZE_T got = 0;
                if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(pos),
                                      buffer.data(), want, &got) && got) {
                    auto findBytes = [&](const std::vector<BYTE>& needle, const char* encoding) {
                        if (needle.empty() || got < needle.size()) return;
                        for (SIZE_T i = 0; i + needle.size() <= got; ++i) {
                            if (memcmp(buffer.data() + i, needle.data(), needle.size()) == 0) {
                                UINT_PTR hit = pos + i;
                                bool duplicate = std::any_of(
                                    hits.begin(), hits.end(),
                                    [hit](const Hit& h) { return h.address == hit; });
                                if (!duplicate) hits.push_back({hit, encoding});
                                if (hits.size() >= 32) return;
                            }
                        }
                    };
                    findBytes(cp932, "CP932");
                    findBytes(utf16, "UTF16");
                    if (hits.size() >= 32) return hits;
                }
                if (got == 0) break;
                pos += got;
            }
        }
        address = end;
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

    const size_t count = std::min<size_t>(addresses.size(), 4);
    for (size_t i = 0; i < count; ++i) {
        switch (i) {
        case 0: c.Dr0 = addresses[i]; break;
        case 1: c.Dr1 = addresses[i]; break;
        case 2: c.Dr2 = addresses[i]; break;
        case 3: c.Dr3 = addresses[i]; break;
        }
        c.Dr7 |= (1u << (i * 2));       // local enable
        c.Dr7 |= (3u << (16 + i * 4));   // RW=read/write, LEN=1 byte
    }
    return SetThreadContext(thread, &c) != FALSE;
}

static void ClearDebugStatus(HANDLE thread) {
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &c)) {
        c.Dr6 = 0;
        SetThreadContext(thread, &c);
    }
}

static void Log(std::wofstream& log, const std::wstring& line) {
    log << line << L"\n";
    log.flush();
}

static void SetBreakpointsForProcessThreads(DWORD pid,
                                            const std::vector<UINT_PTR>& addresses,
                                            std::wofstream& log) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te{sizeof(te)};
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                FALSE, te.th32ThreadID);
            if (!thread) continue;
            bool ok = SetDataBreakpoints(thread, addresses);
            if (!ok) {
                log << L"[BREAKPOINT ERROR] thread=" << te.th32ThreadID << L"\n";
            }
            CloseHandle(thread);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static int RunDebugger(DWORD pid, const std::vector<Hit>& hits,
                        const std::wstring& logPath) {
    std::wofstream log(logPath, std::ios::out | std::ios::trunc);
    if (!log) return 2;

    std::vector<UINT_PTR> addresses;
    for (size_t i = 0; i < hits.size() && i < 4; ++i) addresses.push_back(hits[i].address);

    Log(log, L"=== NOBU6HD STRING TRACE V1 START ===");
    log << L"PID=" << pid << L"\n";
    log << L"TARGET_ADDRESSES=" << addresses.size() << L"\n";
    for (const auto& hit : hits) {
        log << L"[STRING] encoding=";
        if (hit.encoding == "CP932") log << L"CP932";
        else log << L"UTF16";
        log << L" address=" << Hex(hit.address).c_str() << L"\n";
    }

    if (addresses.empty()) {
        Log(log, L"NO_MATCH");
        return 0;
    }

    if (!DebugActiveProcess(pid)) {
        log << L"[DEBUG ATTACH ERROR] win32=" << GetLastError() << L"\n";
        return 3;
    }

    log << L"[DEBUG ATTACHED]\n";
    log << L"[BREAKPOINTS] requested=" << addresses.size() << L"\n";
    log.flush();

    bool running = true;
    int accessEvents = 0;
    while (running) {
        DEBUG_EVENT ev{};
        if (!WaitForDebugEvent(&ev, INFINITE)) {
            log << L"[DEBUG WAIT ERROR] win32=" << GetLastError() << L"\n";
            break;
        }

        DWORD continueStatus = DBG_CONTINUE;

        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (ev.u.CreateProcessInfo.hThread) {
                SetDataBreakpoints(ev.u.CreateProcessInfo.hThread, addresses);
            }
            SetBreakpointsForProcessThreads(pid, addresses, log);
            log << L"[CREATE_PROCESS] thread=" << ev.dwThreadId << L"\n";
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;

        case CREATE_THREAD_DEBUG_EVENT:
            if (ev.u.CreateThread.hThread) {
                SetDataBreakpoints(ev.u.CreateThread.hThread, addresses);
            }
            log << L"[CREATE_THREAD] thread=" << ev.dwThreadId << L"\n";
            break;

        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_DEBUG_INFO& ex = ev.u.Exception;
            if (ex.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP) {
                HANDLE thread = OpenThread(
                    THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                    FALSE, ev.dwThreadId);
                if (thread) {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_DEBUG_REGISTERS | CONTEXT_CONTROL;
                    if (GetThreadContext(thread, &c)) {
                        std::wstring caller = ModuleNameFromAddress(pid, c.Eip);
                        log << L"[STRING ACCESS] thread=" << ev.dwThreadId
                            << L" dr6=" << Hex(c.Dr6).c_str()
                            << L" eip=" << Hex(c.Eip).c_str()
                            << L" caller_module=" << caller << L"\n";
                        ++accessEvents;
                        if (accessEvents >= 2000) {
                            log << L"[TRACE LIMIT] 2000 access events reached\n";
                            running = false;
                        }
                    }
                    ClearDebugStatus(thread);
                    CloseHandle(thread);
                }
            } else if (ex.dwFirstChance == 0) {
                continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }

        case EXIT_THREAD_DEBUG_EVENT:
            break;

        case EXIT_PROCESS_DEBUG_EVENT:
            log << L"[EXIT_PROCESS] code=" << ev.u.ExitProcess.dwExitCode << L"\n";
            running = false;
            break;

        case LOAD_DLL_DEBUG_EVENT:
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            break;

        case UNLOAD_DLL_DEBUG_EVENT:
            break;

        case OUTPUT_DEBUG_STRING_EVENT:
            break;

        default:
            break;
        }

        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
    }

    DebugActiveProcessStop(pid);
    log << L"[DEBUG DETACHED]\n";
    log.flush();
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    const std::wstring target = argc >= 2 ? argv[1] : L"織田信長";
    const std::wstring exeName = L"NOBU6HD_JP.exe";
    const std::wstring logPath = L"nobu_string_trace.txt";

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{sizeof(pe)};
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    std::wofstream log(logPath, std::ios::out | std::ios::trunc);
    if (log) {
        Log(log, L"=== NOBU6HD STRING TRACE V1 START ===");
        log << L"PID=" << pid << L"\n";
        log << L"TARGET=\"" << target << L"\"\n";
        log.flush();
    }

    if (!pid) {
        if (log) Log(log, L"ERROR: NOBU6HD_JP.exe not found");
        std::wcerr << L"NOBU6HD_JP.exe not found.\n";
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, pid);
    if (!process) {
        if (log) log << L"OpenProcess failed. win32=" << GetLastError() << L"\n";
        return 2;
    }

    std::vector<Hit> hits = ScanProcess(process, target);
    CloseHandle(process);

    if (log) {
        log << L"FOUND=" << hits.size() << L"\n";
        for (const auto& hit : hits) {
            log << L"[STRING] encoding="
                << (hit.encoding == "CP932" ? L"CP932" : L"UTF16")
                << L" address=" << Hex(hit.address).c_str() << L"\n";
        }
        log.close();
    }

    if (hits.empty()) {
        std::wcout << L"Target string not found in process memory.\n";
        return 0;
    }

    std::wcout << L"Found " << hits.size()
               << L" candidate(s). Attaching debugger...\n";
    return RunDebugger(pid, hits, logPath);
}
