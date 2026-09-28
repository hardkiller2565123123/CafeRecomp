# tools

User helper modes that live in `dolrecomp.exe`.

## Extract

Disc extraction is exposed through the main executable.

Native support:

- GameCube `.iso` filesystem extraction

External bridge:

- Uses Wiimms ISO Tool (`wit`) for Wii `.iso` and `.wbfs` inputs when native extraction is not enough.
- `dolrecomp.exe --setup` can download a local copy into `extern/wit`.

Other disc formats are rejected. Convert them to `.iso` or `.wbfs` first.

Examples:

```sh
dolrecomp.exe extract game.iso extracted
dolrecomp.exe extract game.wbfs extracted
dolrecomp.exe extract --wit C:\tools\wit\wit.exe game.wbfs extracted
dolrecomp.exe extract --info game.iso
```
where extracted is your output folder.
