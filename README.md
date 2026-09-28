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

CafeRecomp builds on research and work shared by several open-source emulation and recompilation projects.

### [DolRecomp](https://github.com/ExpansionPak/DolRecomp)

A huge thank you to the DolRecomp developers and contributors.

**CafeRecomp uses DolRecomp as the base for its static recompilation framework.** Their work provided the foundation that made CafeRecomp possible without having to build the entire recompilation system from scratch.

### [Cemu](https://github.com/cemu-project/Cemu)

A huge thank you to the Cemu developers and contributors.

Cemu has been an invaluable reference for **Wii U graphics, GX2 behavior, rendering, and system behavior** while developing CafeRecomp's renderer and runtime.

### [gx2gl](https://github.com/ExpansionPak/gx2gl)

Thanks to the gx2gl developers and contributors for their work on Wii U GX2 graphics translation.

Their work has been a useful reference while developing CafeRecomp's graphics support.

### [Aurora](https://github.com/encounter/aurora)

Thanks to the Aurora developers and contributors for making their work and research available to the community.

### [ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template)

Thanks to the developers and contributors behind ModernGekko-Template for their work on recompilation project structure and tooling.

Special thanks to everyone in the emulation, reverse-engineering, preservation, and static recompilation communities who shares their research and source code.

## Status

CafeRecomp is still in active development.

Compatibility is limited, and bugs, crashes, missing graphics, and incomplete functionality should be expected.

Breath of the Wild is the current main target, but the CafeRecomp framework is designed to be reused for other Wii U projects and games.

## Disclaimer

CafeRecomp is an independent preservation and research project and is not affiliated with, endorsed by, or sponsored by Nintendo.

Wii U, Nintendo, The Legend of Zelda, Breath of the Wild, and related names and trademarks belong to their respective owners.

CafeRecomp does not provide copyrighted game files, firmware, encryption keys, or other proprietary Nintendo content.

Users are responsible for providing legally obtained files required for their own use.

## License

See the repository license for details.
