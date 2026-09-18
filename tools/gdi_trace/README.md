# NOBU6HD GDI Render Trace V2

순정 NOBU6HD_JP.exe의 실제 문자 렌더링 경로를 검증하기 위한 비파괴 분석 도구입니다.

## V2에서 추가된 추적

기존 GDI 문자 API 추적에 다음을 추가했습니다.

- `GetProcAddress` — 동적으로 GDI API 주소를 획득하는 경로 확인
- `BitBlt`, `StretchBlt`, `PatBlt`, `AlphaBlend` — 비트맵/래스터 합성 경로 확인
- Injector가 콘솔을 유지하며 각 Win32 오류를 표시

기존 추적: TextOutA/W, ExtTextOutA/W, DrawTextA/W, GetGlyphOutlineA/W, CreateFontA/W, CreateFontIndirectA/W, GetStockObject, SelectObject, GetDIBits.

## 안전성

- 게임의 EXE/DAT/N6P 파일을 수정하지 않습니다.
- 게임 프로세스 메모리의 IAT만 분석 목적으로 변경합니다.
- 로그는 `%TEMP%\nobu_gdi_trace.txt`에 기록합니다.
- 한글 패치 기능은 포함하지 않습니다.

## 사용

1. `HookDLL.dll`과 `Injector.exe`를 같은 폴더에 둡니다.
2. `NOBU6HD_JP.exe`를 실행합니다.
3. 일본어 텍스트가 표시되는 화면까지 진입합니다.
4. 같은 폴더의 `Injector.exe`를 실행합니다.
5. 메인 메뉴, 시나리오 선택, 무장 정보 등 일본어가 많이 표시되는 화면을 몇 초씩 봅니다.
6. 게임을 종료합니다.
7. `%TEMP%\nobu_gdi_trace.txt`를 확인합니다.

V2 Injector는 성공/실패 메시지를 표시하고 Enter를 누를 때까지 창을 유지합니다.

## 해석

- `ExtTextOutA/W` 또는 `TextOutA/W`가 일본어 문자열과 함께 반복되면 GDI 텍스트 출력 경로의 강한 증거입니다.
- `GetGlyphOutlineA/W`가 보이면 GDI가 글리프를 직접 요청하는 경로의 강한 증거입니다.
- `GetProcAddress`에서 위 GDI API 이름이 요청되면 동적 API 획득 경로가 확인됩니다. 단, 이 후킹은 모든 가능한 동적 호출을 보장하지 않습니다.
- `GetStockObject(17)`는 `DEFAULT_GUI_FONT` 요청입니다.
- `GetStockObject(17)` + `SelectObject` + 문자 출력 API가 관찰되면 폰트 선택과 텍스트 출력이 연결된 증거가 됩니다.
- `BitBlt/StretchBlt/AlphaBlend` 등의 호출만 많고 문자 API가 없으면, 게임이 이미 만들어진 비트맵/글리프를 래스터 합성하는 경로일 가능성을 추가로 조사해야 합니다.
- 관련 API가 0회라고 해서 자체 래스터라이저라고 확정하지 않습니다. IAT 후킹과 동적 API 후킹 모두 놓칠 수 있는 구현이 존재합니다.

## 빌드

GitHub Actions가 Windows x86 Release 빌드를 자동 생성합니다. 로컬에서는 Developer Command Prompt에서:

```bat
msbuild NOBU6HD_GDI_Trace.sln /p:Configuration=Release /p:Platform=Win32
```

를 실행할 수 있습니다.
