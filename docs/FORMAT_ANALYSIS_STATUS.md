# N6P format analysis status

## Confirmed from supplied files

- `MSG\\MESSAGE.N6P` and `n6data\\MESSAGE.N6P` are byte-identical in the supplied archive.
- Both begin with the byte signature `4C 53 31 31` (`LS11` in ASCII), followed by zero bytes and binary data.
- The supplied message directory contains multiple `bfile*.dat`, `msgsec*.dat`, `sfile*.dat`, `ifile*.dat`, and `hd_msg0.dat` files.
- `hd_msg0.dat` begins with a sequence of increasing little-endian 16-bit values, which is consistent with an index/table candidate, but this is not yet proven.
- `bfile0.dat` begins with increasing little-endian 32-bit values, also consistent with an offset table candidate, but this is not yet proven.

## Current conclusion

The files are not plain text containers. A safe Korean patch requires identifying the relationship between the index files and the message payload/container, then proving the encoding and record boundaries using cross-file offsets and controlled replacements.

## Next evidence required

1. Original `language.dll` or the executable/resource files that select the language and font.
2. A known in-game message sample mapped to its byte representation.
3. Preferably, two versions of the same message asset with one controlled text change, or a way to trigger a known text change in-game.

No original game assets are committed to this repository. The analyzer is read-only and must be run against user-supplied copies.
