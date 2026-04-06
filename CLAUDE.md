# CLAUDE.md — genKlayoutStack (gks)

## What This Project Is

`genKlayoutStack` (`gks`) is a C++20 command-line tool that manages Klayout enablement
files for semiconductor process technology stacks. It reads and writes:

- **TOML stack definition** (`.toml`) — the canonical source of truth
- **Klayout layer properties file** (`.lyp`) — XML; display properties only
- **Klayout 2.5D Ruby script** (`.rb`) — physical z-extrusion for 3D visualization

The full design is in `docs/PROJECT_SPEC.md` (v0.4.1). Read that document first.
This file tells you **how to implement it**.

---

## Ground Rules

1. **The spec is the source of truth.** If this file and the spec conflict, the spec wins.
   Ask before deviating from either.
2. **No stubs.** Every function you write must compile and do something real, even if
   minimal. Do not emit `// TODO` bodies that return default-constructed values silently.
3. **No silent design changes.** If a spec section is ambiguous or seems wrong, stop and
   say so. Do not paper over it with a local decision.
4. **One phase at a time.** Complete and test one phase fully before beginning the next.
   Each phase must pass `ctest` before you proceed.
5. **Tests are not optional.** Every non-trivial function gets a unit test. Integration
   tests use the fixture files in `fixtures/`.

---

## Repository Layout

```
genKlayoutStack/
  CMakeLists.txt
  CLAUDE.md                    ← this file
  docs/
    PROJECT_SPEC.md            ← read this first
  include/
    gks/
      core/
        LayerStack.hpp
        Validator.hpp
        Defaulter.hpp
      io/
        TomlReader.hpp
        TomlWriter.hpp
        LypReader.hpp
        LypWriter.hpp
        ScriptReader.hpp
        ScriptWriter.hpp
  src/
    core/
      LayerStack.cpp
      buildStack.cpp
      Validator.cpp
      Defaulter.cpp
    io/
      TomlReader.cpp
      TomlWriter.cpp
      LypReader.cpp
      LypWriter.cpp
      ScriptReader.cpp
      ScriptWriter.cpp
    cli/
      main.cpp
      cmd_import.cpp
      cmd_generate.cpp
      cmd_validate.cpp
  tests/
    unit/
      test_buildStack.cpp
      test_validator.cpp
      test_defaulter.cpp
      test_tomlReader.cpp
      test_lypReader.cpp
      test_scriptReader.cpp
      test_scriptWriter.cpp
    integration/
      test_roundtrip_toml_lyp.cpp
      test_roundtrip_toml_script.cpp
      test_bootstrap_import.cpp
      test_negative_z.cpp
      test_parallel_groups.cpp
  fixtures/
    default.lyp                ← real .lyp file; use as LypReader test input
    asap7_stack.toml           ← reference TOML; build this in Phase 3
    asap7_25d.rb               ← generated in Phase 5 from asap7_stack.toml
```

---

## Build System

- CMake 3.20+, C++20
- Dependencies via `FetchContent`:
  - `toml++` (tomlplusplus) — TOML parsing/writing
  - `pugixml` — `.lyp` XML parsing/writing
  - `GoogleTest` — unit and integration tests
- Build and test commands:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Namespaces and Conventions

- All public types and functions live in namespace `gks`
- Internal implementation helpers: anonymous namespace or `gks::detail`
- File names: `PascalCase.hpp` / `PascalCase.cpp` for classes; `camelCase.cpp` for
  free functions (`buildStack.cpp`, `buildStack.hpp` inline in `LayerStack.hpp`)
- Error handling: `std::expected<T, GksError>` for recoverable errors; throw
  `std::runtime_error` at IO boundary only for unrecoverable failures (bad file,
  malformed XML)
- `GksError`: a simple struct with `std::string message` and `int source_line = 0`

---

## Key Types (summary — full detail in spec Section 4.3)

```
gks::Color           r, g, b uint8_t; fromHex("#rrggbb" or "0xrrggbb"); toHex(); toInt()
gks::DisplayProps    fill_color, frame_color, fill_alpha, dither_pattern, line_style,
                     visible, valid, transparent  [TOML-exposed]
                     + frame_brightness, fill_brightness, width, marked, xfill,
                       animation, expanded        [passthrough — .lyp fidelity only]
gks::PhysicalProps   z_start_nm (signed double), thickness_nm, material,
                     layer_expression (optional)
gks::LayerEntry      layer_num, datatype, name, purpose, display, physical (optional)
gks::LayerStack      tech_name, version, layers, defaults
gks::RawLayer        all-optional intermediate; used between readers and buildStack()
```

**Critical invariants:**
- `(layer_num, datatype)` is unique within a `LayerStack` — hard error if violated
- `z_start_nm` is **signed** — negative values are valid and expected
- `dither_pattern = -1` means solid fill; `line_style = -1` means no style
- `physical = nullopt` is valid — display-only layers are legal

---

## The Z Model (critical — read carefully)

`z = 0` is the **bonding plane**. Negative z is below (substrate, backside metal, TSVs).
Positive z is above (FEOL, BEOL).

**Document order convention:** The first `[[layer]]` in the TOML represents the
**top** of the physical stack; the last represents the **bottom**. `buildStack()`
reverses the raw layer vector before the accumulation loop so that accumulation
still runs bottom-to-top (lowest z first).

In `buildStack()`, z is resolved by **hybrid accumulation**:
- If `z_start_nm` is absent in `RawLayer` → use `high_water` (running max of z_top seen so far)
- If `z_start_nm` is present → use it directly (explicit override)
- `high_water = max(high_water, z_start + thickness_nm)` after each physical layer
- `high_water` is a signed double; initializes to `0.0`

Parallel layers share the same explicit `z_start_nm`. The tool detects these and reports
them in diagnostics. See spec Section 5.2 for the full pseudocode and all edge cases.

---

## The .lyp XML Schema

Root element: `<layer-properties>`. Each layer is a `<properties>` child.
Full field mapping is in spec Section 4.4. Key points:

- `<source>` format: `"layer_num/datatype@1"` — always emit `@1` on write
- `<n>` — layer name; optional, may be empty; populate when present
- `<dither-pattern>` — `"I{n}"` or empty; parse int after `I`; -1 if empty
- `<line-style>` — `"L{n}"` or empty; parse int after `L`; -1 if empty
- `<fill-brightness>` / `<frame-brightness>` — typically 0; passthrough faithfully
- Bool elements use `"true"` / `"false"` strings; parse case-insensitively
- Emit elements in the order shown in spec Table 4.4 to match Klayout output

---

## The 2.5D Script Format

Standard layer:
```ruby
z(input(6, 0), zstart: 150.0.nm, height: 36.0.nm, fill: 0x0055ff, frame: 0x0033cc, name: "m0")
```

Parallel group (`layer_expression` set on entry):
```ruby
zz(name: "contacts", fill: 0xff8800, frame: 0xcc6600) do
  z(input(4,0), zstart: 105.0.nm, height: 45.0.nm)
  z(input(5,0), zstart: 105.0.nm, height: 45.0.nm)
end
```

Writer rules:
- Always emit z values with `.nm` suffix
- Emit `fill:` and `frame:` as `0xrrggbb` hex integers
- Omit `name:` if `LayerEntry.name` is empty
- Sort by `z_start_nm` ascending at emit time (do not mutate IR order)
- Negative z emits correctly: `zstart: -300.0.nm`
- Prepend the gks header comment block (see spec Section 5.4)

Reader scope: **gks-generated scripts only** (literal values, no Ruby variables in
argument positions). Scripts with variables emit WARNING and parse best-effort.
Unit suffixes: `.nm` × 1, `.um` × 1000, `.mm` × 1_000_000, bare number → assume nm + INFO.

---

## CLI Subcommands

```
gks import   [--lyp <f>] [--script <f>] -o <out.toml> [--prefer-script-colors] [--verbose]
gks generate <stack.toml> [--lyp <f>] [--script <f>] [--verbose]
gks validate <stack.toml> [--for lyp|3d|full] [--verbose]
```

At least one output/input flag required per subcommand. See spec Section 6 for full detail
and usage examples.

---

## Implementation Phases

Work through these in order. Do not start a phase until the previous one passes `ctest`.

---

### Phase 0 — Skeleton

**Goal:** Repo compiles, links, and `ctest` passes a smoke test. Nothing functional yet.

Tasks:
1. `CMakeLists.txt` — project setup, C++20, FetchContent for toml++, pugixml, GoogleTest,
   install targets, ctest integration
2. All `.hpp` / `.cpp` files created with correct include guards, namespace `gks`,
   empty class/function bodies that compile (no stub return silences — use `= delete`
   or `static_assert(false)` on anything not yet implemented so the compiler complains
   if it's accidentally called)
3. `tests/unit/test_smoke.cpp` — a single `TEST(Smoke, Compiles) { SUCCEED(); }`
4. `fixtures/default.lyp` — copy the provided file into place
5. Confirm: `cmake --build build && ctest --test-dir build` exits 0

Deliverable: green `ctest` on a clean build.

---

### Phase 1 — Core Data Model + buildStack

**Goal:** All core types defined; `buildStack()` fully implemented and tested.

Tasks:
1. `include/gks/core/LayerStack.hpp` — all structs: `Color`, `DisplayProps`, `PhysicalProps`,
   `LayerEntry`, `LayerStack`, `RawLayer`, `GksError`
   - `Color::fromHex`, `Color::toHex`, `Color::toInt` implemented in header or `.cpp`
2. `src/core/buildStack.cpp` — implement `buildStack()` per spec Section 5.2 pseudocode
   - Signed high_water
   - Explicit vs. accumulated z
   - Burial and gap warnings
   - Parallel group detection in diagnostic output
3. `tests/unit/test_buildStack.cpp`:
   - Sequential stack accumulates correctly from z=0
   - Sequential stack accumulates correctly from negative z start
   - Parallel group: two entries same explicit z_start, high_water advances past both
   - Mixed stack spanning bonding plane (negative z start, accumulates through 0 to positive)
   - Burial warning: explicit z_start below high_water
   - Gap warning: explicit z_start above high_water
   - Document-level default thickness applied when per-layer thickness absent
   - All-display-only stack (no physical fields): all `physical = nullopt`
   - `layer_expression` passes through to `PhysicalProps`

Deliverable: all Phase 1 tests green.

---

### Phase 2 — Validator + Defaulter

**Goal:** Tiered validation and defaulting work correctly.

Tasks:
1. `src/core/Validator.cpp` — implement four tiers per spec Section 5.3:
   - `validate_identity()` — duplicate key detection; blank name at INFO level
   - `validate_for_lyp()` — DisplayProps range checks
   - `validate_for_3d()` — PhysicalProps presence and range; overlapping z warning
   - `validate_full()` — all of the above
   - Return type: `std::vector<GksDiagnostic>` where `GksDiagnostic` has level
     (ERROR/WARN/INFO), message, source_line
2. `src/core/Defaulter.cpp` — apply `LayerStack::Defaults` to entries missing fields;
   log each substitution as an INFO diagnostic
3. `tests/unit/test_validator.cpp` — one test per validation rule
4. `tests/unit/test_defaulter.cpp` — default thickness, fill_alpha, material, dither_pattern
   applied correctly; per-layer value takes precedence over default

Deliverable: all Phase 2 tests green.

---

### Phase 3 — TOML I/O

**Goal:** Full round-trip `TOML → LayerStack → TOML` is lossless.

Tasks:
1. `src/io/TomlReader.cpp` — parse TOML into `vector<RawLayer>` using toml++
   - Parse `[stack]` and `[stack.defaults]` into `LayerStack` metadata and `Defaults`
   - Parse each `[[layer]]` block into `RawLayer`
   - `source_line` populated from toml++ node source location
   - `Color::fromHex` used for color fields
   - `dither_pattern` and `line_style` stored as plain integers
2. `src/io/TomlWriter.cpp` — write `LayerStack` back to TOML
   - Always emit explicit `z_start_nm` for all layers with physical data
   - Emit passthrough DisplayProps fields? **No** — TOML exposes only the
     TOML-exposed subset (see spec Section 4.3 DisplayProps notes)
   - Emit a comment `# z_start_nm computed by accumulation` vs
     `# z_start_nm explicit` — helpful for user review
3. Build `fixtures/asap7_stack.toml` — a representative 8–10 layer stack with:
   - Two negative-z backside layers
   - One parallel group (two layers, same z_start_nm)
   - At least one layer using document-level default thickness
4. `tests/unit/test_tomlReader.cpp` — parse `asap7_stack.toml`; verify all fields
5. `tests/integration/test_roundtrip_toml_lyp.cpp` — `TOML → LayerStack → TOML`;
   compare key fields (not string identity — field-by-field)

Deliverable: all Phase 3 tests green; `asap7_stack.toml` committed to fixtures.

---

### Phase 4 — .lyp I/O

**Goal:** Full round-trip `.lyp → LayerStack → .lyp` is lossless; `default.lyp` round-trips cleanly.

Tasks:
1. `src/io/LypReader.cpp` — parse `.lyp` XML per spec Section 5.5 and field table 4.4
   - `<source>` → `(layer_num, datatype)`; strip `@N` suffix
   - `<dither-pattern>` → parse int after `I`; empty element → -1
   - `<line-style>` → parse int after `L`; empty element → -1
   - `<n>` → name; may be empty; that is fine
   - All passthrough fields read and stored in `DisplayProps`
   - `physical = nullopt` for all entries
2. `src/io/LypWriter.cpp` — write `LayerStack` to `.lyp` XML
   - Emit elements in field table order (spec Section 4.4)
   - `<source>` format: `"layer_num/datatype@1"`
   - `<dither-pattern>` → `"I{n}"` or empty element if -1
   - `<line-style>` → `"L{n}"` or empty element if -1
   - `<width>` → empty element if `nullopt`
   - Bool fields → `"true"` / `"false"` strings
3. `tests/unit/test_lypReader.cpp`
   - Parse `fixtures/default.lyp`; verify layer count, spot-check several entries
     (layer_num, datatype, fill_color, dither_pattern)
   - Verify all entries have `physical = nullopt`
   - Verify empty `<n>` produces `name = ""`
4. `tests/integration/test_roundtrip_toml_lyp.cpp`
   - `asap7_stack.toml → LypWriter → LypReader → LayerStack`
   - Verify DisplayProps fields round-trip; PhysicalProps absent after read-back
5. Validate `fill_alpha` → `fill_brightness` mapping empirically against Klayout;
   document findings in spec as a note under `DisplayProps`

Deliverable: all Phase 4 tests green; `default.lyp` round-trips with zero field loss.

---

### Phase 5 — 2.5D Script I/O

**Goal:** Full round-trip `TOML → script → TOML (physical only)` is lossless;
`zz()` blocks emit and parse correctly; negative z works.

Tasks:
1. `src/io/ScriptWriter.cpp` — per spec Section 5.4
   - Sort layers by `z_start_nm` ascending (copy, do not mutate IR)
   - Skip layers with `physical = nullopt`; note skipped layers in header comment
   - Standard `z()` emit for normal layers
   - `zz()` block emit when `layer_expression` is set
   - Negative z emits correctly (`zstart: -300.0.nm`)
   - Emit gks header comment block
2. `src/io/ScriptReader.cpp` — per spec Section 5.6
   - Scan for `z(` and `zz(` call sites
   - Extract `input(L, DT)`, `zstart`, `height`/`zstop`, `fill`, `frame`, `name`
   - Unit suffix conversion (`.nm`, `.um`, `.mm`, bare)
   - `zz()` block parsing: extract inner `z()` calls; build `layer_expression`
   - Warn on apparent Ruby variable usage
3. Build `fixtures/asap7_25d.rb` by running `gks generate` on `asap7_stack.toml`
   and committing the output
4. `tests/unit/test_scriptWriter.cpp`
   - Standard layer emit
   - Negative z emit
   - `zz()` block emit for parallel group
   - Layer without physical props skipped
   - Layers sorted by z_start_nm in output
5. `tests/unit/test_scriptReader.cpp`
   - Parse `fixtures/asap7_25d.rb`; verify physical fields match `asap7_stack.toml`
   - Unit suffix conversion: `.nm`, `.um`
   - `zz()` block produces `layer_expression`
6. `tests/integration/test_roundtrip_toml_script.cpp`
   - `asap7_stack.toml → ScriptWriter → ScriptReader → RawLayer vector`
   - Verify z_start_nm, thickness_nm, material round-trip for all physical layers
7. `tests/integration/test_negative_z.cpp`
   - Stack with layers at negative z; verify writer emits negative zstart;
     reader parses it back correctly
8. `tests/integration/test_parallel_groups.cpp`
   - Stack with one parallel group; verify `zz()` emitted and parsed;
     `layer_expression` survives round-trip

Deliverable: all Phase 5 tests green; `asap7_25d.rb` committed.

---

### Phase 6 — CLI

**Goal:** All three subcommands work end-to-end; UC1–UC5 pass functional tests.

Tasks:
1. `src/cli/main.cpp` — top-level subcommand dispatch; use a simple hand-rolled
   argument parser (no heavy CLI library needed given the small surface)
2. `src/cli/cmd_import.cpp` — `gks import` per spec Section 6.2
   - At least one of `--lyp` / `--script` required; error if neither
   - Merge behavior: `.lyp` colors take precedence unless `--prefer-script-colors`
   - Warn on layers present in one input but not the other
   - Warn on blank names after merge
   - Write output TOML
3. `src/cli/cmd_generate.cpp` — `gks generate` per spec Section 6.3
   - Run full pipeline: TomlReader → buildStack → Validator (first pass) →
     Defaulter → Validator (second pass) → requested writers
   - `--lyp` only: validate_for_lyp; no PhysicalProps required
   - `--script` only or both: validate_for_3d or validate_full
4. `src/cli/cmd_validate.cpp` — `gks validate` per spec Section 6.4
   - No output files; print diagnostics to stdout; exit code 1 on any ERROR
5. Verbose mode (`--verbose`): print all INFO diagnostics; default is WARN and above only
6. `tests/integration/test_bootstrap_import.cpp`
   - `gks import --lyp default.lyp --script asap7_25d.rb -o merged.toml`
   - Verify merged TOML has correct display + physical fields
   - `gks generate merged.toml --lyp out.lyp --script out.rb`
   - Verify outputs match originals field-by-field

Deliverable: all Phase 6 tests green; `gks --help` prints sensible usage.

---

## What Done Looks Like

The project is complete when:
- `ctest --test-dir build` passes all tests with zero failures
- `gks import --lyp fixtures/default.lyp -o /tmp/test.toml` runs without error,
  produces valid TOML with correct layer count
- `gks generate fixtures/asap7_stack.toml --lyp /tmp/out.lyp --script /tmp/out.rb`
  runs without error and produces files that load in Klayout without complaint
- Round-trip identity holds: running `generate` twice from the same TOML produces
  byte-identical output files

---

## Frequently Needed Spec Sections

| Topic                          | Spec section |
|-------------------------------|--------------|
| All core type definitions      | 4.3          |
| .lyp XML field mapping table   | 4.4          |
| TOML format + example          | 4.5          |
| buildStack pseudocode          | 5.2          |
| Validation tiers               | 5.3          |
| ScriptWriter rules             | 5.4          |
| LypReader parsing rules        | 5.5          |
| ScriptReader parsing rules     | 5.6          |
| CLI subcommand specs           | 6.1–6.5      |
