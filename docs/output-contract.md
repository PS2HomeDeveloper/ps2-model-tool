# Output Contract

All files are little-endian, IEEE-754 32-bit floats, sections aligned to 16 bytes, no pointers.
Files are written only when the source has the data for them.

| File | Written when | Magic / version |
|------|--------------|-----------------|
| `NAME.mesh` | the model has meshes | `GMSH` / 1 |
| `NAME.skeleton` | the model has deformation bindings | `GSKL` / 1 |
| `NAME.anim` | the model has bindings and at least one animation stack | `GANM` / 2 |
| `NAME.material` | the model has materials | `GMAT` / 1 |
| `report.json` | always | format_version 2 |

## .mesh
Header 64 bytes: magic, version, file size, vertex count, index count, part count, section offsets, bounds.
Vertex (48 bytes): `float position[3]`, `float normal[3]`, `float uv[2]`, `uint16 joints[4]`, `uint8 weights[4]` (sum 255), 4 reserved.
Indices are `uint32`, three per triangle. Part (16 bytes): first index, index count, material, flags.
The model is normalized to right-handed Y-up, height 2.0, feet on Y=0, centered on X/Z.

## .skeleton
Binding (128 bytes): `int32 parent`, `uint32 node id`, `char name[64]`, `float geometry_to_bone[3][4]`. Parents precede children.

## .anim (version 2, all clips in one file)
```text
0   char     magic[4] = "GANM"
4   uint32   version  = 2
8   uint32   file size
12  uint32   binding count
16  uint32   clip count
20  uint32   clip table offset (32)
24  uint8    reserved[8]
32  clip table, 64 bytes per clip:
      char    name[32]
      uint32  frame count
      float   fps
      float   duration      ((frames - 1) / fps)
      uint32  data offset   (absolute, 16-byte aligned)
      uint8   reserved[16]
```
Clip data is `float matrix[frame][binding][12]`, row-major 3x4 affine matrices.
Both endpoints of each clip are sampled. Each clip has its own length, taken from its keyframes.
Bindings match `.skeleton` one to one. A runtime can load the file once and pick a clip by name or index, or read only the table and stream one clip.

## .material
Header 32 bytes. Record (128 bytes): `char name[48]`, `char texture[48]`, `float diffuse[4]`, `uint32 flags`, 12 reserved.
Flags: bit 0 transparent blending, bit 1 double-sided. An empty texture name means flat color.

## report.json
Human-readable metadata: reference clip, every clip with frames and duration, geometry counts, weights, normalization, materials, warnings.
