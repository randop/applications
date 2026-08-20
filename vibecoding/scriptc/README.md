Real comparison, all measured on the same box, doing the same "print + idle 3s" job:

| Build | Binary size | RSS while running |
|---|---|---|
| **scriptc** (static native) | ~420 KB | **~2 MB** |
| Node SEA | ~120 MB | ~57 MB |
| Plain `node` interpreting the JS | n/a (needs Node installed) | ~43 MB |

Roughly a **20-30x** memory reduction, not just a smaller file on disk. scriptc has no V8, no GC heap warming up, no JS object model sitting in memory. It's just compiled machine code with a thin native runtime underneath.

