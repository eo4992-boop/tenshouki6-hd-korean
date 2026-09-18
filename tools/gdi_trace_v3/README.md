# NOBU6HD GDI TRACE V3

V3 is the next runtime-tracing stage for the Tenshouki 6 HD Korean-patch investigation.

V2 showed GetProcAddress=117 while TextOut/ExtTextOut/DrawText/GetGlyphOutline/CreateFont were all 0. V3 therefore focuses on the actual names resolved through GetProcAddress and DLLs loaded at runtime.

V3 records GetProcAddress target names, owning module, ordinal lookups, LoadLibraryA/W names, and resolved addresses.

Output: %TEMP%\NOBU6HD_GDI_TRACE_V3.log

This component is read-only and does not modify game files or N6P resources. The repository does not contain the copyrighted game executable or assets.