#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

struct Hit { UINT_PTR address{}; std::string encoding; };
struct Watch { UINT_PTR address{}; std::string encoding; };

static std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring DefaultTarget() {
    const BYTE cp932[] = { 0x90,0x44,0x93,0x63,0x90,0x4D,0x92,0xB7 };
    int n = MultiByteToWideChar(932, 0, reinterpret_cast<LPCSTR>(cp932), sizeof(cp932), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<LPCSTR>(cp932), sizeof(cp932), out.data(), n);
    return out;
}

static std::string Hex(UINT_PTR v) {
    std::ostringstream s; s << "0x" << std::hex << std::uppercase << v; return s.str();
}

static std::vector<BYTE> ToCP932(const std::wstring& s) {
    int n = WideCharToMultiByte(932, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::vector<BYTE> v(n);
    WideCharToMultiByte(932, 0, s.data(), (int)s.size(), reinterpret_cast<LPSTR>(v.data()), n, nullptr, nullptr);
    return v;
}

static std::vector<BYTE> ToUtf16(const std::wstring& s) {
    std::vector<BYTE> v(s.size() * sizeof(wchar_t));
    if (!v.empty()) memcpy(v.data(), s.data(), v.size());
    return v;
}

static bool IsReadable(DWORD protect) {
    DWORD p = protect & 0xFF;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static std::vector<Hit> Scan(HANDLE p, const std::wstring& target, std::ofstream& log) {
    std::vector<Hit> hits;
    auto cp = ToCP932(target), u16 = ToUtf16(target);
    std::vector<BYTE> buf(1024 * 1024);
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    UINT_PTR a = (UINT_PTR)si.lpMinimumApplicationAddress, mx = (UINT_PTR)si.lpMaximumApplicationAddress;

    while (a < mx) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQueryEx(p, (LPCVOID)a, &m, sizeof(m))) break;
        UINT_PTR b = (UINT_PTR)m.BaseAddress, e = b + m.RegionSize;
        if (e <= a) break;

        bool readable = m.State == MEM_COMMIT && !(m.Protect & PAGE_GUARD) &&
                        m.Protect != PAGE_NOACCESS && IsReadable(m.Protect);
        if (readable) {
            for (UINT_PTR pos = b; pos < e;) {
                SIZE_T want = (SIZE_T)std::min<UINT_PTR>(buf.size(), e - pos), got = 0;
                if (ReadProcessMemory(p, (LPCVOID)pos, buf.data(), want, &got) && got) {
                    auto find = [&](const std::vector<BYTE>& needle, const char* enc) {
                        if (needle.empty() || got < needle.size()) return;
                        for (SIZE_T i = 0; i + needle.size() <= got; ++i) {
                            if (memcmp(buf.data() + i, needle.data(), needle.size()) == 0) {
                                UINT_PTR h = pos + i;
                                if (std::none_of(hits.begin(), hits.end(), [h](const Hit& x){ return x.address == h; }))
                                    hits.push_back({h, enc});
                            }
                        }
                    };
                    find(cp, "CP932"); find(u16, "UTF16");
                    if (hits.size() >= 64) return hits;
                }
                if (!got) break;
                pos += got;
            }
        }
        a = e;
    }
    return hits;
}

static std::string ModuleInfo(DWORD pid, UINT_PTR address) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) return "<unknown>";
    MODULEENTRY32W me{sizeof(me)}; std::string r = "<unknown>";
    if (Module32FirstW(s, &me)) do {
        UINT_PTR b = (UINT_PTR)me.modBaseAddr, e = b + me.modBaseSize;
        if (address >= b && address < e) {
            std::ostringstream x; x << Utf8(me.szModule) << " eip=" << Hex(address) << " rva=" << Hex(address - b);
            r = x.str(); break;
        }
    } while (Module32NextW(s, &me));
    CloseHandle(s); return r;
}

static std::string CodeBytes(HANDLE p, UINT_PTR address) {
    BYTE bytes[16]{}; SIZE_T got = 0;
    if (!ReadProcessMemory(p, (LPCVOID)address, bytes, sizeof(bytes), &got) || !got) return "<unreadable>";
    std::ostringstream x; x << std::hex << std::uppercase << std::setfill('0');
    for (SIZE_T i = 0; i < got; ++i) { if (i) x << ' '; x << std::setw(2) << (unsigned)bytes[i]; }
    return x.str();
}

static bool SetBP(HANDLE t, const std::vector<Watch>& watches) {
    CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS | CONTEXT_CONTROL;
    if (!GetThreadContext(t, &c)) return false;

    c.Dr0 = c.Dr1 = c.Dr2 = c.Dr3 = 0;
    c.Dr6 = 0; c.Dr7 = 0;

    for (size_t i = 0; i < watches.size() && i < 4; ++i) {
        UINT_PTR x = watches[i].address & ~(UINT_PTR)3;
        if (i == 0) c.Dr0 = x;
        if (i == 1) c.Dr1 = x;
        if (i == 2) c.Dr2 = x;
        if (i == 3) c.Dr3 = x;
        c.Dr7 |= 1u << (i * 2);          // local enable
        c.Dr7 |= 3u << (16 + i * 4);     // read/write
        c.Dr7 |= 3u << (18 + i * 4);     // 4-byte length
    }
    return SetThreadContext(t, &c) != FALSE;
}

static bool ApplyToAllThreads(DWORD pid, const std::vector<Watch>& watches, std::ofstream& log) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    bool ok = true; THREADENTRY32 te{sizeof(te)};
    if (Thread32First(s, &te)) do {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) { ok = false; continue; }
        if (!SetBP(t, watches)) {
            log << "[BP ERROR] tid=" << te.th32ThreadID << " win32=" << GetLastError() << "\n";
            ok = false;
        }
        CloseHandle(t);
    } while (Thread32Next(s, &te));
    CloseHandle(s); return ok;
}

static void Log(std::ofstream& log, const std::string& s) {
    if (log) { log << s << "\n"; log.flush(); }
    std::cout << s << "\n";
}

static void Pause(const char* m, DWORD e) {
    std::cerr << "\nERROR: " << m << " (Win32=" << e << ")\nPress Enter to exit...";
    std::string x; std::getline(std::cin, x);
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    const std::wstring target = argc >= 2 ? argv[1] : DefaultTarget();
    const std::string t8 = Utf8(target);
    const std::string logPath = "nobu_runtime_string_trace.txt";

    std::ofstream log(logPath, std::ios::binary | std::ios::trunc);
    if (log) { const unsigned char bom[] = {0xEF,0xBB,0xBF}; log.write((const char*)bom, 3); }
    Log(log, "=== NOBU6HD RUNTIME STRING FLOW TRACE V1 START ===");
    Log(log, "TARGET=\"" + t8 + "\"");
    Log(log, "MODE=LAUNCH_UNDER_DEBUGGER");

    std::wstring exe = L"NOBU6HD_JP.exe";
    std::vector<wchar_t> cmd(exe.begin(), exe.end()); cmd.push_back(L'\0');
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    DWORD flags = DEBUG_ONLY_THIS_PROCESS;

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        Pause("CreateProcessW failed. Put this tracer next to NOBU6HD_JP.exe.", GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    DWORD pid = pi.dwProcessId;
    Log(log, "[PROCESS CREATED] pid=" + std::to_string(pid));

    std::vector<Hit> hits;
    std::vector<Watch> watches;
    bool resumed = false, run = true;
    size_t group = 0;
    const size_t GROUPS = 0; // calculated after scan
    ULONGLONG lastRotate = GetTickCount64();

    while (run) {
        DEBUG_EVENT ev{};
        if (!WaitForDebugEvent(&ev, 250)) {
            DWORD e = GetLastError();
            if (e == ERROR_SEM_TIMEOUT) {
                continue;
            }
            Log(log, "[DEBUG WAIT ERROR] win32=" + std::to_string(e));
            break;
        }

        DWORD continueStatus = DBG_CONTINUE;

        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT: {
            Log(log, "[DEBUG CREATE_PROCESS] pid=" + std::to_string(ev.dwProcessId) + " tid=" + std::to_string(ev.dwThreadId));
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);

            HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!p) {
                Log(log, "[SCAN ERROR] OpenProcess win32=" + std::to_string(GetLastError()));
            } else {
                hits = Scan(p, target, log);
                CloseHandle(p);
                Log(log, "[SCAN] FOUND=" + std::to_string(hits.size()));
                for (const auto& h : hits)
                    Log(log, "[STRING] encoding=" + h.encoding + " address=" + Hex(h.address));
            }

            if (!hits.empty()) {
                group = 0;
                for (size_t i = 0; i < hits.size() && i < 4; ++i) watches.push_back({hits[i].address, hits[i].encoding});
                bool ok = ev.u.CreateProcessInfo.hThread && SetBP(ev.u.CreateProcessInfo.hThread, watches);
                Log(log, "[BP INSTALL] group=1/" + std::to_string((hits.size()+3)/4) +
                         " requested=" + std::to_string(watches.size()) + " ok=" + std::to_string(ok ? 1 : 0));
                resumed = true;
                lastRotate = GetTickCount64();
            }
            break;
        }

        case CREATE_THREAD_DEBUG_EVENT:
            Log(log, "[DEBUG CREATE_THREAD] tid=" + std::to_string(ev.dwThreadId));
            if (!watches.empty() && ev.u.CreateThread.hThread)
                Log(log, "[BP NEW THREAD] tid=" + std::to_string(ev.dwThreadId) +
                         " ok=" + std::to_string(SetBP(ev.u.CreateThread.hThread, watches) ? 1 : 0));
            break;

        case EXCEPTION_DEBUG_EVENT: {
            const auto& ex = ev.u.Exception;
            DWORD code = ex.ExceptionRecord.ExceptionCode;

            if (code == EXCEPTION_SINGLE_STEP) {
                HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ev.dwThreadId);
                if (t) {
                    CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS | CONTEXT_CONTROL;
                    if (GetThreadContext(t, &c)) {
                        int dr = -1;
                        for (int i = 0; i < 4; ++i) if (c.Dr6 & (1u << i)) { dr = i; break; }
                        HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                        std::string module = ModuleInfo(pid, c.Eip);
                        std::ostringstream line;
                        line << "[STRING ACCESS] tid=" << ev.dwThreadId
                             << " dr=" << dr << " dr6=" << Hex(c.Dr6)
                             << " eip=" << Hex(c.Eip) << " " << module;
                        if (p) { line << " bytes=" << CodeBytes(p, c.Eip); CloseHandle(p); }
                        Log(log, line.str());
                        c.Dr6 = 0; SetThreadContext(t, &c);
                    }
                    CloseHandle(t);
                }
            } else {
                std::ostringstream line;
                line << "[DEBUG EXCEPTION] code=" << Hex(code)
                     << " first_chance=" << ex.dwFirstChance
                     << " address=" << Hex((UINT_PTR)ex.ExceptionRecord.ExceptionAddress);
                Log(log, line.str());
                if (code == EXCEPTION_BREAKPOINT && !resumed) {
                    // Keep the initial debugger breakpoint from terminating the process.
                }
            }
            break;
        }

        case LOAD_DLL_DEBUG_EVENT:
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            break;

        case EXIT_THREAD_DEBUG_EVENT:
            Log(log, "[DEBUG EXIT_THREAD] tid=" + std::to_string(ev.dwThreadId));
            break;

        case EXIT_PROCESS_DEBUG_EVENT:
            Log(log, "[DEBUG EXIT_PROCESS] code=" + Hex(ev.u.ExitProcess.dwExitCode));
            run = false;
            break;

        default:
            break;
        }

        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);

        // Rotate only after the game has actually started. This keeps the first group
        // installed before the very first instruction after CREATE_PROCESS is continued.
        if (resumed && !hits.empty() && hits.size() > 4 && GetTickCount64() - lastRotate >= 600) {
            size_t groups = (hits.size() + 3) / 4;
            group = (group + 1) % groups;
            watches.clear();
            size_t begin = group * 4;
            for (size_t i = begin; i < hits.size() && i < begin + 4; ++i)
                watches.push_back({hits[i].address, hits[i].encoding});
            bool ok = ApplyToAllThreads(pid, watches, log);
            Log(log, "[BP ROTATE] group=" + std::to_string(group + 1) + "/" + std::to_string(groups) +
                     " requested=" + std::to_string(watches.size()) + " ok=" + std::to_string(ok ? 1 : 0));
            lastRotate = GetTickCount64();
        }
    }

    if (pi.hProcess) CloseHandle(pi.hProcess);
    Log(log, "=== TRACE END ===");
    std::cout << "Trace ended. Log: " << logPath << "\nPress Enter to exit...";
    std::string x; std::getline(std::cin, x);
    return 0;
}
