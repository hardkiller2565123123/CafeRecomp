# Contributing to DolRecomp

Look, forks are welcome if you want to mess around with the recompiler codebase, but **we are not accepting random Pull Requests right now.** The codebase is moving and changing way too fast during this early sprint. 

Save yourself the time and don't open a PR unless a maintainer explicitly tells you to do so in an issue or on the Discord `#dolrecomp` channel. 

[![Discord Banner](https://discord.com/api/guilds/1508777745709269034/widget.png?style=banner2)](https://discord.gg/MMQJ4TdmFs)

## Code Quality & Rules

If you are writing code for local forks, patches, or future submissions, keep these strict guidelines in mind:

*   **No AI-Generated Code:** Absolutely no copy-pasted junk from Claude, Grok, ChatGPT, or any other LLM. Write the code yourself. If it looks like machine-slop, it’s getting tossed.
*   **Write Non-Vague Comments:** Don't leave useless comments that explain *what* the obvious syntax is doing. Leave helpful, concrete comments explaining *why* a specific decoding choice or backend logic step was implemented.

## Filing Issues & Code of Conduct

GitHub Issues are strictly for reproducible bugs, build failures, incorrect C codegen output, missing instruction support, and other actionable problems. 

### Do Not Use Issues to Complain
Issues are not your personal diary for non-technical complaints, drive-by trolling, or drama. We’ve already had to lock and close completely useless tickets from people complaining about the project tagline, structure, or lack of comments. 

If you open an issue just to whine, say *"it doesn't work"*, or throw a tantrum without technical details, it will be closed and ignored instantly. 

### Your issue must include:
1. **The input type being tested:** (DOL, RPX, or REL)
2. **The failing instruction or function:** (If you know it)
3. **The exact commit/build used:** (Don't just say "latest")
4. **Steps to reproduce:** (What did you run to make it choke?)

## Local Environment & Building

If you are setting up the repo to debug code generation or fix things locally, make sure you meet the baseline requirements:

*   **CMake 3.16** or newer
*   A **C11 compiler**
*   **zlib** (Optional, but mandatory if you are testing compressed Wii U RPX sections)

### Build Pipeline:
```bash
cmake -S . -B build
cmake --build build --config Release -j 14
ctest --test-dir build -C Release --output-on-failure
```

*Note: If you are messing with Wii U recompilation, devkitPro is highly recommended.*

## Code Structure

Before you start poking around the source tree, here is where everything lives:
*   `src/frontend/` — DOL/RPX loading and PowerPC instruction decoding.
*   `src/backend/` — Split C generation and manifest output.
*   `tests/` — Decoder validation, CPU behavior, and codegen test cases.
