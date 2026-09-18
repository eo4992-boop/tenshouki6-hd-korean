#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstring>

struct Hit { UINT_PTR address{}; std::string encoding; };

static std::string Utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr);
    return out;
}
static std::wstring DefaultTarget() {
    const BYTE cp932[] = { 0x90,0x44,0x93,0x63,0x90,0x4D,0x92,0xB7 };
    int n=MultiByteToWideChar(932,0,reinterpret_cast<LPCSTR>(cp932),sizeof(cp932),nullptr,0);
    if(n<=0)return {};
    std::wstring out(n,L'\0');
    MultiByteToWideChar(932,0,reinterpret_cast<LPCSTR>(cp932),sizeof(cp932),out.data(),n);
    return out;
}
static std::string Hex(UINT_PTR v){std::ostringstream s;s<<"0x"<<std::hex<<std::uppercase<<v;return s.str();}
static std::vector<BYTE> ToCP932(const std::wstring& s){
    int n=WideCharToMultiByte(932,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::vector<BYTE> v(n);
    WideCharToMultiByte(932,0,s.data(),(int)s.size(),reinterpret_cast<LPSTR>(v.data()),n,nullptr,nullptr);
    return v;
}
static std::vector<BYTE> ToUtf16(const std::wstring& s){
    std::vector<BYTE> v(s.size()*sizeof(wchar_t));
    if(!v.empty())memcpy(v.data(),s.data(),v.size());
    return v;
}
static std::vector<Hit> Scan(HANDLE p,const std::wstring& target){
    std::vector<Hit> hits;auto cp=ToCP932(target),u16=ToUtf16(target);std::vector<BYTE> buf(1024*1024);
    SYSTEM_INFO si{};GetSystemInfo(&si);UINT_PTR a=(UINT_PTR)si.lpMinimumApplicationAddress,mx=(UINT_PTR)si.lpMaximumApplicationAddress;
    while(a<mx){
        MEMORY_BASIC_INFORMATION m{};if(!VirtualQueryEx(p,(LPCVOID)a,&m,sizeof(m)))break;
        UINT_PTR b=(UINT_PTR)m.BaseAddress,e=b+m.RegionSize;if(e<=a)break;
        DWORD pr=m.Protect&0xFF;bool rd=m.State==MEM_COMMIT&&!(m.Protect&PAGE_GUARD)&&pr!=PAGE_NOACCESS;
        if(rd)for(UINT_PTR pos=b;pos<e;){
            SIZE_T want=(SIZE_T)std::min<UINT_PTR>(buf.size(),e-pos),got=0;
            if(ReadProcessMemory(p,(LPCVOID)pos,buf.data(),want,&got)&&got){
                auto find=[&](const std::vector<BYTE>& n,const char* enc){
                    if(n.empty()||got<n.size())return;
                    for(SIZE_T i=0;i+n.size()<=got;i++)if(memcmp(buf.data()+i,n.data(),n.size())==0){
                        UINT_PTR h=pos+i;
                        if(std::none_of(hits.begin(),hits.end(),[h](const Hit&x){return x.address==h;}))hits.push_back({h,enc});
                        if(hits.size()>=64)return;
                    }
                };
                find(cp,"CP932");find(u16,"UTF16");if(hits.size()>=64)return hits;
            }
            if(!got)break;pos+=got;
        }
        a=e;
    }
    return hits;
}
static std::string ModuleInfo(DWORD pid,UINT_PTR address){
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(s==INVALID_HANDLE_VALUE)return "<unknown>";
    MODULEENTRY32W me{sizeof(me)};std::string r="<unknown>";
    if(Module32FirstW(s,&me))do{
        UINT_PTR b=(UINT_PTR)me.modBaseAddr,e=b+me.modBaseSize;
        if(address>=b&&address<e){std::ostringstream x;x<<Utf8(me.szModule)<<" eip="<<Hex(address)<<" rva="<<Hex(address-b);r=x.str();break;}
    }while(Module32NextW(s,&me));
    CloseHandle(s);return r;
}
static bool SetBP(HANDLE t,const std::vector<UINT_PTR>& a){
    CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS|CONTEXT_CONTROL;
    if(!GetThreadContext(t,&c))return false;
    c.Dr0=c.Dr1=c.Dr2=c.Dr3=0;c.Dr6=0;c.Dr7=0;
    for(size_t i=0;i<a.size()&&i<4;i++){
        UINT_PTR x=a[i]&~(UINT_PTR)3;
        switch(i){case 0:c.Dr0=x;break;case 1:c.Dr1=x;break;case 2:c.Dr2=x;break;case 3:c.Dr3=x;break;}
        c.Dr7|=1u<<(i*2);
        c.Dr7|=3u<<(16+i*4);
        c.Dr7|=3u<<(18+i*4);
    }
    if(!SetThreadContext(t,&c))return false;
    CONTEXT v{};v.ContextFlags=CONTEXT_DEBUG_REGISTERS;
    if(!GetThreadContext(t,&v))return false;
    return v.Dr7==c.Dr7;
}
static void SetAll(DWORD pid,const std::vector<UINT_PTR>& a,std::ofstream& log){
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(s==INVALID_HANDLE_VALUE){log<<"[THREAD SNAPSHOT ERROR] "<<GetLastError()<<"\n";return;}
    THREADENTRY32 te{sizeof(te)};
    if(Thread32First(s,&te))do{
        if(te.th32OwnerProcessID!=pid)continue;
        HANDLE t=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,te.th32ThreadID);
        if(!t){log<<"[THREAD OPEN ERROR] thread="<<te.th32ThreadID<<" win32="<<GetLastError()<<"\n";continue;}
        if(!SetBP(t,a))log<<"[BREAKPOINT ERROR] thread="<<te.th32ThreadID<<" win32="<<GetLastError()<<"\n";
        CloseHandle(t);
    }while(Thread32Next(s,&te));
    CloseHandle(s);
}
static void Pause(const char* m,DWORD e){
    std::cerr<<"\nERROR: "<<m<<" (Win32="<<e<<")\nPress Enter to exit...";
    std::string x;std::getline(std::cin,x);
}

int wmain(int argc,wchar_t* argv[]){
    SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
    const std::wstring target=argc>=2?argv[1]:DefaultTarget();
    const std::string t8=Utf8(target),logPath="nobu_string_trace_v2.txt";
    std::ofstream log(logPath,std::ios::binary|std::ios::trunc);
    if(log){const unsigned char bom[]={0xEF,0xBB,0xBF};log.write((const char*)bom,3);}
    std::cout<<"=== NOBU6HD STRING TRACE V2 START ===\nTARGET=\""<<t8<<"\"\n";
    if(log)log<<"=== NOBU6HD STRING TRACE V2 START ===\nTARGET=\""<<t8<<"\"\n";
    const std::wstring exe=L"NOBU6HD_JP.exe";DWORD pid=0;
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(s!=INVALID_HANDLE_VALUE){
        PROCESSENTRY32W pe{sizeof(pe)};
        if(Process32FirstW(s,&pe))do{if(_wcsicmp(pe.szExeFile,exe.c_str())==0){pid=pe.th32ProcessID;break;}}while(Process32NextW(s,&pe));
        CloseHandle(s);
    }
    if(!pid){Pause("NOBU6HD_JP.exe not found",0);return 1;}
    std::cout<<"PID="<<pid<<"\n";if(log)log<<"PID="<<pid<<"\n";
    HANDLE p=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!p){DWORD e=GetLastError();Pause("OpenProcess failed",e);return 2;}
    auto hits=Scan(p,target);CloseHandle(p);
    std::cout<<"FOUND="<<hits.size()<<"\n";if(log)log<<"FOUND="<<hits.size()<<"\n";
    for(auto&h:hits){std::cout<<"[STRING] encoding="<<h.encoding<<" address="<<Hex(h.address)<<"\n";if(log)log<<"[STRING] encoding="<<h.encoding<<" address="<<Hex(h.address)<<"\n";}
    if(hits.empty()){if(log)log<<"NO_MATCH\n";std::cout<<"Press Enter to exit...";std::string x;std::getline(std::cin,x);return 0;}
    std::vector<UINT_PTR> addr;
    for(size_t i=0;i<hits.size()&&i<4;i++)addr.push_back(hits[i].address);
    if(!DebugActiveProcess(pid)){
        DWORD e=GetLastError();if(log)log<<"[DEBUG ATTACH ERROR] win32="<<e<<"\n";Pause("DebugActiveProcess failed",e);return 3;
    }
    if(log)log<<"[DEBUG ATTACHED]\n";
    std::cout<<"[DEBUG ATTACHED]\n";
    SetAll(pid,addr,log);
    if(log)log<<"[BREAKPOINTS] requested="<<addr.size()<<"\n";
    std::cout<<"[BREAKPOINTS] requested="<<addr.size()<<"\n";
    bool run=true;
    while(run){
        DEBUG_EVENT ev{};
        if(!WaitForDebugEvent(&ev,INFINITE)){
            DWORD e=GetLastError();if(log)log<<"[DEBUG WAIT ERROR] win32="<<e<<"\n";break;
        }
        DWORD cs=DBG_CONTINUE;
        switch(ev.dwDebugEventCode){
        case CREATE_PROCESS_DEBUG_EVENT:
            if(log)log<<"[DEBUG CREATE_PROCESS] pid="<<ev.dwProcessId<<"\n";
            if(ev.u.CreateProcessInfo.hThread){bool ok=SetBP(ev.u.CreateProcessInfo.hThread,addr);if(log)log<<"[BP VERIFY] tid="<<ev.dwThreadId<<" ok="<<(ok?1:0)<<"\n";}
            if(ev.u.CreateProcessInfo.hFile)CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            if(log)log<<"[DEBUG CREATE_THREAD] tid="<<ev.dwThreadId<<"\n";
            if(ev.u.CreateThread.hThread){bool ok=SetBP(ev.u.CreateThread.hThread,addr);if(log)log<<"[BP VERIFY] tid="<<ev.dwThreadId<<" ok="<<(ok?1:0)<<"\n";}
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if(log)log<<"[DEBUG LOAD_DLL]\n";
            if(ev.u.LoadDll.hFile)CloseHandle(ev.u.LoadDll.hFile);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            if(log)log<<"[DEBUG UNLOAD_DLL]\n";
            break;
        case EXCEPTION_DEBUG_EVENT:{
            auto& ex=ev.u.Exception;
            if(ex.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP){
                HANDLE t=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,ev.dwThreadId);
                if(t){
                    CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS|CONTEXT_CONTROL;
                    if(GetThreadContext(t,&c)){
                        int dr=-1;for(int i=0;i<4;i++)if(c.Dr6&(1u<<i)){dr=i;break;}
                        std::string info=ModuleInfo(pid,c.Eip);
                        if(log)log<<"[STRING ACCESS] thread="<<ev.dwThreadId<<" dr="<<dr<<" dr6="<<Hex(c.Dr6)<<" "<<info<<"\n";
                        std::cout<<"[STRING ACCESS] thread="<<ev.dwThreadId<<" dr="<<dr<<" "<<info<<"\n";
                        c.Dr6=0;SetThreadContext(t,&c);
                    }
                    CloseHandle(t);
                }
            }else{
                if(log)log<<"[DEBUG EXCEPTION] code="<<Hex(ex.ExceptionRecord.ExceptionCode)<<" first_chance="<<ex.dwFirstChance<<"\n";
                if(ex.dwFirstChance==0)cs=DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        case EXIT_THREAD_DEBUG_EVENT:
            if(log)log<<"[DEBUG EXIT_THREAD] tid="<<ev.dwThreadId<<"\n";
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            if(log)log<<"[DEBUG EXIT_PROCESS] code="<<Hex(ev.u.ExitProcess.dwExitCode)<<"\n";
            run=false;break;
        default:break;
        }
        ContinueDebugEvent(ev.dwProcessId,ev.dwThreadId,cs);
    }
    DebugActiveProcessStop(pid);
    if(log)log<<"[DEBUG DETACHED]\n";
    std::cout<<"[DEBUG DETACHED]\nTrace ended. Log: "<<logPath<<"\nPress Enter to exit...";
    std::string x;std::getline(std::cin,x);
    return 0;
}
