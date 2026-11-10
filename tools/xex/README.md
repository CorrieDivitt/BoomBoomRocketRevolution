# XEX tools

Small Python helpers used for the analysis in `docs/ANALYSIS.md`. They read your own dump; they contain no game data.

| Script | Purpose |
|---|---|
| `xexinfo.py <default.xex>` | Print XEX2 optional headers, static libraries, resources, entry point |
| `unxex.py <default.xex> <out.lzx>` | Decrypt (retail key) and de-chunk the LZX stream; prints `window_bits image_size compressed_size` |
| `lzx.c` | Tiny libmspack wrapper: `lzx <in.lzx> <out.pe> <window_bits> <image_size>` |
| `analyze.py <default.xex> <image.pe>` | Imports by name (via ReXGlue export tables), PE sections, `.pdata` function count, save/restore helper candidates, instruction stats |

Build `lzx` (needs the SDK submodules for libmspack):
```
gcc -O2 -Ithirdparty/rexglue-sdk/thirdparty/libmspack/libmspack/mspack -o lzx tools/xex/lzx.c \
    thirdparty/rexglue-sdk/thirdparty/libmspack/libmspack/mspack/lzxd.c thirdparty/rexglue-sdk/thirdparty/libmspack/libmspack/mspack/system.c
```
Requires `pip install cryptography`.
