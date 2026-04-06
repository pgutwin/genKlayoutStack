---
monofont: "Menlo"
geometry: margin=0.6in
---
# genKlayoutStack (gks)

A C++20 command-line tool for generating and synchronizing
[KLayout](https://www.klayout.de) enablement files from a single canonical
technology stack definition.

---

## What it does

Keeping a KLayout layer properties file (`.lyp`) and a 2.5D visualization
script in sync by hand is tedious and error-prone. `gks` solves this by
treating a TOML file as the single source of truth for both display properties
and physical layer geometry, and generating both artifacts from it.

```
stack.toml  ──►  layout.lyp      (layer colors, stipples, visibility)
            ──►  stack_3d.rb     (KLayout 2.5D Ruby script for 3D visualization)

layout.lyp  ──►  stack.toml      (migration: import existing .lyp into gks workflow)
layout.lyp  +
stack_3d.rb ──►  stack.toml      (bootstrap: import both artifacts at once)
```

---

## Features

- **TOML-first workflow** — one file defines colors, stipples, z positions,
  thicknesses, and materials for the entire process stack
- **Hybrid z accumulation** — sequential layers need only a thickness; explicit
  `z_start_nm` is required only for parallel (co-planar) layers and backside
  structures
- **Signed z coordinates** — full support for wafer-bonded stacks where z = 0
  is the bonding plane and backside layers have negative z
- **Parallel layer groups** — two or more design layers sharing the same
  physical z range (e.g. epi contact and gate contact) are handled natively
- **Bootstrap import** — bring an existing `.lyp` + 2.5D script under `gks`
  management in a single command
- **Tiered validation** — display-only, 3D, or full validation with
  ERROR / WARN / INFO diagnostics before any file is written
- **Document-level defaults** — set default thickness, material, or stipple
  once; override per layer as needed

---

## Quick start

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Generate .lyp and 2.5D script from a TOML definition
./build/gks generate stack.toml --lyp out.lyp --script out_3d.rb

# Import an existing .lyp into the gks workflow
./build/gks import --lyp existing.lyp -o stack.toml

# Bootstrap from both existing artifacts
./build/gks import --lyp existing.lyp --script existing_3d.rb -o stack.toml

# Validate before generating
./build/gks validate stack.toml --for full --verbose
```

---

## The TOML stack definition

A minimal stack definition looks like this:

```toml
[stack]
tech_name = "my_process"
version   = "1.0.0"

[stack.defaults]
thickness_nm = 36.0
material     = "metal"

[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
fill_color   = "#006600"
frame_color  = "#004400"
# z_start_nm omitted -- accumulated from high_water after parallel group
# thickness_nm omitted -- inherited from [stack.defaults]

[[layer]]
name         = "gate_contact"
layer_num    = 5
datatype     = 0
fill_color   = "#8800FF"
frame_color  = "#6600CC"
z_start_nm   = 22.0     # explicit: same z = parallel group
thickness_nm = 37.0
material     = "tungsten"

[[layer]]
name         = "epi_contact"
layer_num    = 4
datatype     = 0
fill_color   = "#FF8800"
frame_color  = "#CC6600"
z_start_nm   = 22.0     # explicit: parallel group
thickness_nm = 37.0
material     = "tungsten"

[[layer]]
name         = "poly"
layer_num    = 3
datatype     = 0
fill_color   = "#FFAA00"
frame_color  = "#CC8800"
thickness_nm = 22.0
material     = "poly"
```

See [`docs/gks_z_stack_cheatsheet.md`](docs/gks_z_stack_cheatsheet.md) for
a full explanation of the z-positioning model, worked examples, and a
quick-reference table.

---

## CLI reference

### `gks generate`

Generate `.lyp` and/or 2.5D Ruby script from a TOML stack definition.

```
gks generate <stack.toml> [--lyp <file>] [--script <file>] [--verbose]
```

At least one of `--lyp` or `--script` is required.

```bash
# Both outputs
gks generate stack.toml --lyp out.lyp --script out.rb

# Display only (no physical properties required in TOML)
gks generate stack.toml --lyp out.lyp

# With full diagnostic output
gks generate stack.toml --lyp out.lyp --script out.rb --verbose
```

### `gks import`

Create a TOML stack definition from an existing `.lyp` and/or 2.5D script.

```
gks import [--lyp <file>] [--script <file>] -o <output.toml>
           [--prefer-script-colors] [--verbose]
```

At least one of `--lyp` or `--script` is required. When both are provided,
display properties come from the `.lyp` and physical properties come from the
script. Pass `--prefer-script-colors` to use the script's color values instead.

```bash
# Migrate from .lyp only (fill in physical fields in the TOML afterward)
gks import --lyp existing.lyp -o stack.toml

# Full bootstrap from both artifacts
gks import --lyp existing.lyp --script existing.rb -o stack.toml
```

**Note:** `gks import --script` only parses gks-generated scripts (literal
values). Hand-written scripts with Ruby variables or control flow are parsed
best-effort with a warning.

### `gks validate`

Validate a TOML stack definition without writing any output files.
Exit code is 1 if any ERROR-level diagnostic is emitted.

```
gks validate <stack.toml> [--for lyp|3d|full] [--verbose]
```

| Mode    | Checks |
|---------|--------|
| `lyp`   | Identity + display properties only; physical not required |
| `3d`    | Identity + physical properties; thickness, z, material required |
| `full`  | All checks (default) |

---

## Typical workflow

```bash
# One-time: bootstrap from existing KLayout files
gks import --lyp orig.lyp --script orig.rb -o stack.toml

# Fill in any blank layer names, review z positions and thicknesses...

# Daily: edit TOML, regenerate
gks generate stack.toml --lyp new.lyp --script new.rb --verbose
```

---

## Building from source

**Requirements:**
- C++20 compiler (GCC 12+, Clang 14+, MSVC 2022+)
- CMake 3.20+

Dependencies are fetched automatically via CMake `FetchContent`:
- [toml++](https://github.com/marzer/tomlplusplus) — TOML parsing and writing
- [pugixml](https://pugixml.org) — `.lyp` XML parsing and writing
- [GoogleTest](https://github.com/google/googletest) — testing

```bash
# Debug build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

# Release build
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

---

## Repository layout

```
genKlayoutStack/
  CLAUDE.md              -- implementation guide for Claude Code
  docs/
    PROJECT_SPEC.md      -- full design specification
    gks_z_stack_cheatsheet.md
    z_semantics.svg
    asap7_stack.svg
    high_water_flow.svg
  include/gks/
    core/                -- LayerStack IR, Validator, Defaulter
    io/                  -- readers and writers
  src/
    core/
    io/
    cli/
  tests/
    unit/
    integration/
  fixtures/
    default.lyp          -- reference .lyp for reader tests
    asap7_stack.toml     -- reference stack for integration tests
    asap7_25d.rb         -- generated 2.5D script from asap7_stack.toml
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [`docs/PROJECT_SPEC.md`](docs/PROJECT_SPEC.md) | Full design specification (v0.4.1) |
| [`docs/gks_z_stack_cheatsheet.md`](docs/gks_z_stack_cheatsheet.md) | How to define layer positions in TOML |
| [`CLAUDE.md`](CLAUDE.md) | Implementation guide and phase plan for Claude Code |

To render the cheat sheet as a PDF:

```bash
pandoc docs/gks_z_stack_cheatsheet.md \
  -o gks_z_stack_cheatsheet.pdf \
  --pdf-engine=xelatex
```

The YAML front matter in the cheat sheet sets `monofont: "Menlo"` for
macOS. Change to `"DejaVu Sans Mono"` on Linux
(`brew install --cask font-dejavu` on Mac).

---

## Status

Work in progress. See [`docs/PROJECT_SPEC.md`](docs/PROJECT_SPEC.md) for the
current phase plan.

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Skeleton — CMake, blank modules, smoke test | planned |
| 1 | Core data model + `buildStack()` | planned |
| 2 | Validator + Defaulter | planned |
| 3 | TOML I/O | planned |
| 4 | `.lyp` XML I/O | planned |
| 5 | 2.5D Ruby script I/O | planned |
| 6 | CLI (`import`, `generate`, `validate`) | planned |

---

## License

TBD

---

## Author

Paul Gutwin
