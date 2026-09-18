# NOBU6HD GDI Render Trace

순정 NOBU6HD_JP.exe의 실제 문자 렌더링 경로를 검증하기 위한 비파괴 분석 도구입니다.

## 추적 API

TextOutA/W, ExtTextOutA/W, DrawTextA/W, GetGlyphOutlineA/W, CreateFontA/W, CreateFontIndirectA/W, GetStockObject, SelectObject, GetDIBits를 추적합니다.

특히 ExtTextOutA/W 또는 TextOutA/W가 일본어 문자열과 함께 호출되는지, GetGlyphOutlineA/W 또는 GetDIBits가 글리프 생성 시점에 호출되는지가 중요합니다.

## 안전성

- 게임의 EXE/DAT/N6P 파일을 수정하지 않습니다.
- 메모리상의 IAT(import address table)만 분석 목적으로 변경합니다.
- 로그는 %TEMP%\\nobu_gdi_trace.txt에 기록합니다.
- 한글 패치 기능은 포함하지 않습니다.

## 빌드

GitHub Actions가 Windows x86 Release 빌드를 자동 생성합니다. 로컬에서는 Developer Command Prompt에서 다음을 실행할 수 있습니다.

msbuild NOBU6HD_GDI_Trace.sln /p:Configuration=Release /p:Platform=Win32

## 사용

1. NOBU6HD_JP.exe를 실행합니다.
2. 일본어 텍스트가 표시되는 화면까지 진입합니다.
3. 같은 폴더의 Injector.exe를 실행합니다.
4. 메인 메뉴, 시나리오 선택, 무장 정보 등 일본어가 많이 표시되는 화면을 몇 초씩 봅니다.
5. 게임을 종료합니다.
6. %TEMP%\\nobu_gdi_trace.txt를 확인합니다.

예상 로그 항목: ExtTextOutA, GetGlyphOutlineA, GetStockObject type=17, GetDIBits 등.

## 해석

- ExtTextOutA/W 또는 TextOutA/W가 반복되고 일본어 문자열이 보이면 GDI 텍스트 출력 경로의 강한 증거입니다.
- GetGlyphOutlineA/W가 보이면 GDI가 글리프를 직접 요청하는 경로의 강한 증거입니다.
- GetStockObject(17) + SelectObject + ExtTextOutA/W + GetDIBits가 관찰되면 HANDOVER_NOTE의 GDI 가설과 부합합니다.
- 관련 API가 0회라면 이 IAT 방식으로 동적 API 획득을 놓쳤을 가능성이 있으므로, 그 결과만으로 자체 래스터라이저 가설을 확정하지 않습니다.

이 도구의 목적은 먼저 저위험 방식으로 GDI 경로를 재현하는 것입니다.
