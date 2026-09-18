# NOBU6HD String Trace V1

게임 화면에 실제로 표시되는 특정 문자열을 **프로세스 메모리에서 찾고**, 찾은 주소에 x86 하드웨어 데이터 브레이크포인트를 걸어 **어떤 게임 코드(EIP)가 그 문자열 메모리에 접근하는지** 기록하는 비파괴 분석 도구입니다.

## 목적

GDI API가 0회인 현재 상황에서 렌더링 API를 더 추측하지 않고:

`織田信長` 같은 실제 게임 문자열
→ 런타임 메모리 주소
→ 그 주소를 읽거나 쓰는 CPU 명령(EIP)
→ `NOBU6HD_JP.exe + RVA`

순서로 역추적합니다.

## 사용법

1. `NOBU6HD_JP.exe`를 실행합니다.
2. **게임 화면에 추적할 문자열이 실제로 보이는 상태**까지 이동합니다.
3. `StringTrace.exe`를 게임과 같은 폴더에서 실행합니다.
4. 기본 대상은 `織田信長`입니다.
5. 다른 문자열을 추적하려면:
   `StringTrace.exe "시나리오"`
6. 프로그램이 메모리를 검색한 뒤 최대 4개의 후보 주소에 데이터 브레이크포인트를 설정합니다.
7. 게임에서 해당 문자열이 다시 그려지도록 화면을 이동하거나 메뉴를 열고 몇 초 기다립니다.
8. 게임을 종료하면 `nobu_string_trace.txt`를 확인합니다.

## 출력 예

```
=== NOBU6HD STRING TRACE V1 START ===
PID=1234
TARGET="織田信長"
FOUND=2
[STRING] encoding=CP932 address=0x12345678
[STRING] encoding=UTF16 address=0x23456789
[BREAKPOINTS] hardware_data_breakpoints=2
[STRING ACCESS] thread=1234 dr6=0x1 eip=0x401234 caller_module=NOBU6HD_JP.exe caller=0x401234 caller_rva=0x1234
```

여기서 가장 중요한 값은:

`caller_module=NOBU6HD_JP.exe`
`caller_rva=0x1234`

입니다. 이것이 확인되면 게임 EXE의 해당 코드 위치를 정적으로 분석할 수 있습니다.

## 주의

- 32비트(Win32) 빌드입니다.
- 하드웨어 데이터 브레이크포인트는 CPU당 최대 4개 주소만 동시에 추적합니다.
- 문자열이 메모리에 없는 시점에 실행하면 `NO_MATCH`가 나옵니다. 반드시 **문자열이 화면에 표시되는 상태**에서 실행하세요.
- 같은 문자열의 후보가 여러 개이면 처음 발견된 최대 4개를 추적합니다.
- RTSS 등 오버레이 DLL은 이번 단계의 목표가 아닙니다.
- 게임 파일(EXE/DAT/N6P)은 수정하지 않습니다.
- 디버거 방식이므로 다른 디버거가 이미 게임에 붙어 있으면 attach가 실패할 수 있습니다.
