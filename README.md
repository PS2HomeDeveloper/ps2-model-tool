# PS2 Model Tool

A standalone command-line tool that converts 3D models into compact binary resources for PlayStation 2 homebrew (ps2sdk / gsKit).
It is independent of any game. Input: FBX (binary or ASCII, via [ufbx](https://github.com/ufbx/ufbx)). More formats can be added later.

This does not mean every FBX can be converted. Inputs must fit the limits in `include/gr_limits.h` and the runtime supports
triangulated polygon geometry, linear/rigid matrix skinning, supported material fields, and separately available textures.
The converter rejects dual-quaternion skinning, morph/cache deformation, multiple sequential skin deformers, excessive
hierarchy depth, and resources over budget. Layered/procedural color maps that cannot reduce to one file texture are also
rejected. Embedded-only color maps that the current output format cannot emit now fail with an explicit error instead of
silently becoming flat-color materials. Use `--inspect` first to see stacks, geometry counts, materials, and early validation
errors before producing files.

## Build

Needs only a C compiler and make:

```sh
make
```

The executable is `build/bin/ps2-model-tool`.

## Use

```sh
ps2-model-tool MODEL.fbx                 # output goes to ./MODEL
ps2-model-tool MODEL.fbx out_dir         # choose the output directory
ps2-model-tool MODEL.fbx out_dir tm2     # record texture names with the .tm2 extension
ps2-model-tool --help
```

Without a texture format, texture names keep the original image extension from the model.
The tool records texture names; it does not convert images. Supply existing TIM2 files with `--textures DIR` to validate
them and detect transparency. Otherwise, the named texture files must be prepared separately.

Optional: `--textures DIR` checks ready TIM2 files (CT16/CT32, power of two, up to 512) and detects transparency.
Other options: `--fps N`, `--clip NAME`, `--double-sided TEXTURE`, `--inspect`, `--version`.

## Output

Only the files the model needs are written:

| File | Content |
|------|---------|
| `NAME.mesh` | geometry, 4-bone byte-weighted skinning |
| `NAME.skeleton` | bindings and bind matrices |
| `NAME.anim` | **all** animation clips of the FBX in one file |
| `NAME.material` | materials and texture names |
| `report.json` | what was converted, clip list, warnings |

Format details: [docs/output-contract.md](docs/output-contract.md).

## Design notes

- Animation is evaluated on the host, so the PS2 only interpolates matrix samples; no FBX pivot or inheritance logic is needed at runtime.
- One `.anim` file holds every clip with a table of names, lengths and offsets: one file to load, clips selectable by name, each clip has its own length.
- The animated model's palette is normalized with the `idle` clip (or the first clip, or `--clip`) so every clip shares one coordinate frame. Static models are baked into that normalized bind-pose space and do not get a fake animation file.
- New meshes use GMSH v2 with 16-bit indices, reducing index storage by half versus the previous 32-bit layout. The example runtime still accepts v1 resources.
- Animation samples lie on a uniform `1/fps` time grid; the last sample is the last grid point not later than the clip end, keeping PS2 interpolation timing consistent.
- Limits (vertices, bones, frames, texture size) are in `include/gr_limits.h`; exceeding them is an error, never silent truncation.

## Related tools

Part of a PS2 development tool series, with `ps2-tim2-tool` for textures.

## License

MIT for this tool; ufbx is bundled under its own license in `third_party/`.
