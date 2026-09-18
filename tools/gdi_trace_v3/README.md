# NOBU6HD GDI Trace V3

V3 is the runnable Win32 successor to the V2 tracer.

## Files

- `NOBU6HD_GDI_Trace_V3.exe` — starts the injector and injects the tracer into the running `NOBU6HD_JP.exe`.
- `NOBU6HD_GDI_TRACE_V3.dll` — injected tracer.
- Log: `%TEMP%\\NOBU6HD_GDI_TRACE_V3.log`

## What changed from V2

V2 showed zero calls for TextOut/ExtTextOut/DrawText/GetGlyphOutline/CreateFont while recording GetProcAddress calls. V3 keeps the V2 IAT hooks and additionally records:

- every named `GetProcAddress` request (module, function name, returned address)
- `LoadLibraryA/W` calls and DLL names
- repeated IAT scans for modules loaded after injection
- the existing GDI text/bitmap API counters

The build is **Win32/x86**, matching the V2 artifact and the 32-bit game executable.

## Test

1. Start `NOBU6HD_JP.exe`.
2. Run `NOBU6HD_GDI_Trace_V3.exe`.
3. Press Enter in the tracer window.
4. Play/open screens containing Japanese text for about one minute.
5. Close the game.
6. Send `%TEMP%\\NOBU6HD_GDI_TRACE_V3.log` here.

This is read-only tracing. It does not modify N6P files or the game executable.
