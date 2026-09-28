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

## Building

CafeRecomp is currently intended for development and testing.

Build instructions and project requirements are included with the source code.

## Credits

Special thanks to the projects and developers whose work helped make CafeRecomp possible:

- [DolRecomp](https://github.com/ExpansionPak/DolRecomp) — base recompilation framework
- [Cemu](https://github.com/cemu-project/Cemu) — Wii U and rendering reference
- [gx2gl](https://github.com/ExpansionPak/gx2gl) — GX2 graphics reference
- [Aurora](https://github.com/encounter/aurora) — GX2 Rendering
- [ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template) - just Exist

Thank you to everyone who contributed to these projects and shared their work with the community.

## Disclaimer

CafeRecomp is an independent preservation and research project and is not affiliated with, endorsed by, or sponsored by Nintendo.

Wii U, Nintendo, The Legend of Zelda, Breath of the Wild, and related names and trademarks belong to their respective owners.

CafeRecomp does not provide copyrighted game files, firmware, encryption keys, or other proprietary Nintendo content.

Users are responsible for providing legally obtained files required for their own use.

## License

See the repository license for details.
