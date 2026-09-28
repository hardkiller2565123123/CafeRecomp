# gx2gl - an OpenGL layer over GX2

Work-In-Progress OpenGL implementation for the Nintendo Wii U. 

Inspired by VitaGL and my thirst for Sonic 3 A.I.R. on the Wii U

https://github.com/rinnegatamante/vitagl

CafeGLSL: https://github.com/Exzap/CafeGLSL

wut: https://github.com/devkitPro/wut/

"Because ANGLE wasn't enough." - siahisaforker, February 12, 2026  

-# even though angle is something completely different anyway..

# Please read!!

This project is not production ready yet. The goal is OpenGL 3.3 core behavior on Wii U, but gx2gl will not advertise a Khronos GL version until mandatory systems are actually implemented.

## Testing

`gl33_test.rpx` is the Piglit runner. Build it with:

```powershell
cmake --build build_shader_msys --target gl33_test
```

The runner writes the Piglit summary to `gx2gl_results.txt`, individual case
results to `gx2gl_piglit.txt`, and completion to `gx2gl_done.flag` on the SD
card root. The old project-specific test suite is not part of the runner.

For the broad Piglit corpus, generate the SD payload from an upstream Piglit
checkout:

```powershell
python tools/generate_piglit_manifest.py --piglit-root C:\Users\josiah\Documents\piglit
```

Copy `dist\piglit_sd\gx2gl` to the SD card root. `gl33_test.rpx` loads
`/vol/external01/gx2gl/piglit_manifest.tsv`, records every listed case, runs
the shader_test cases it currently understands, and marks unsupported Piglit
case types as explicit skips.

For Cemu, shard manifests are useful because the emulator/compiler can fall
over after thousands of runtime shader compiles in one process:

```powershell
python tools/generate_piglit_manifest.py --piglit-root C:\Users\josiah\Documents\piglit --shard-size 100
```

## Performance

This has only really been tested with Sonic 3 A.I.R. so far, which is kind of a worst-case stress test and not the best performance benchmark.

If you need maximum performance, interface with GX2 directly. If you want to skip the absolute horror of GX2 and actually get something rendering first, this is a good start.
