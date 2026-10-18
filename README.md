# CafeRecomp

CafeRecomp is an open-source Wii U recompilation project focused on bringing Wii U games to modern PCs through static recompilation and a custom runtime.

The project is currently focused on **The Legend of Zelda: Breath of the Wild**, but CafeRecomp is being built as a reusable Wii U recompilation framework rather than a game-specific project.

The same framework can be adapted for other Wii U titles, with game-specific support built on top of the shared recompilation, runtime, and rendering systems.

## Breath of the Wild

**The Legend of Zelda: Breath of the Wild** is currently the main development target for CafeRecomp.

Current work is focused on getting the game through the boot process, improving runtime compatibility, and continuing development of the graphics and rendering systems.

Breath of the Wild is still in development and is not currently considered fully playable.

## Current Progress

Current work includes:

- Wii U PowerPC static recompilation
- RPX and RPL support
- Native PC execution
- Custom Wii U runtime
- Custom renderer
- GX2 graphics support
- Shader support
- Texture and surface support
- Render targets and depth handling
- Filesystem support
- Memory management
- Input support
- Runtime debugging and logging
- Breath of the Wild boot and compatibility work

CafeRecomp is still in active development, and many parts of the Wii U environment are still being implemented.

The release source now includes the consolidated runtime, shader and surface
renderer updates, thread-join/loading fixes, expanded scene shader caches, and
regression tests from the development checkout. These changes do not constitute
a verified fully playable release; gameplay loading and rendering still need work.

## Building

CafeRecomp is currently intended for development and testing.

Install Visual Studio 2026 with the Desktop development with C++ workload and
CMake tools. Supply your own extracted North American Wii U BOTW base game,
v208 update, and DLC under `game/base`, `game/update`, and `game/dlc`.

Run `build.bat` to generate and build. Other commands are:

- `build.bat generate`: regenerate the game code from the supported update RPX.
- `build.bat test`: run the built regression tests.
- `build.bat package`: create a source-only ZIP under `build/releases`.

The game launcher is `build/bin/botw_recomp.exe`, beside its runtime, graphics,
audio, and system DLLs. Development executables are placed in `build/tools`.
The compiler and required support sources are integrated under `src`; separate
third-party project checkouts and PowerShell build scripts are no longer needed.
See `README` for the detailed development notes and `SOURCE-ORIGINS` for provenance.

Game assets, saves, generated game code, and compiled binaries are not included
in this source repository. Generation and compilation require substantial time
and disk space.

## Credits

Special thanks to the projects and developers whose work helped make CafeRecomp possible:

- [DolRecomp](https://github.com/ExpansionPak/DolRecomp) — base recompilation framework
- [Cemu](https://github.com/cemu-project/Cemu) — Wii U and rendering reference
- [gx2gl](https://github.com/ExpansionPak/gx2gl) — GX2 graphics reference
- [Aurora](https://github.com/encounter/aurora) — GX2 Rendering
- [ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template) - just Exist

The integrated sources also include portable surface-calculation code from
ZeldaWWHDRecomp and support code from zlib and fmt. Their license notices are
retained in `licenses` and alongside the source. The complete WWHD Vulkan/Metal
renderer is not connected to CafeRecomp's D3D11 backend.

Thank you to everyone who contributed to these projects and shared their work with the community.

## Disclaimer

CafeRecomp is an independent preservation and research project and is not affiliated with, endorsed by, or sponsored by Nintendo.

Wii U, Nintendo, The Legend of Zelda, Breath of the Wild, and related names and trademarks belong to their respective owners.

CafeRecomp does not provide copyrighted game files, firmware, encryption keys, or other proprietary Nintendo content.

Users are responsible for providing legally obtained files required for their own use.

## License

See the repository license for details.
