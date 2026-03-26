# Project: genKlayoutStack

## 0. Meta & Status

- **Owner:** Paul Gutwin
- **Doc status:** Draft
- **Last updated:** <2026-03-26>
- **Change log:**
  - <2026-03-20> – Initial draft
  - <2026-03-25> – Architecture, data model, z-ordering decisions from design review
  - <2026-03-26> – TOML canonical format; CLI subcommand design; UC4; script import revised;
                    negative Z / bonding plane model added
  - <2026-03-26> – OQ2/OQ4/OQ5 closed; DisplayProps completed from real .lyp schema analysis;
                    .lyp field mapping documented; zz() support confirmed for v1;
                    name-is-empty-in-lyp behavior documented
  - <2026-03-26> – Section 4.4 `<name>` is now optional; Section 5.3 `validate_identity()` - blank name now `[info]`

---

## 1. Project Overview

### 1.1 Problem Statement

When working with Klayout, there are times when one needs to modify the enablement files,
specifically the layer definition file. Klayout itself is fine for making small adjustments,
but when one is working on a complex project and wishes to visualize everything in 3D,
keeping the layer map and 3D (2.5D in Klayout terms) expansion macro in sync becomes
complicated.

What is needed is a tool that can create a consistent set of enablement files (`.lyp` and
2.5D Ruby script) from a single structured definition file and vice versa.

**Decision:** A TOML file is the canonical representation of the technology stack. Both the
`.lyp` and the 2.5D Ruby script are derived output artifacts. This is discussed further in
Section 3.

### 1.2 The Z Coordinate System and the Bonding Plane

Modern process technologies — particularly those involving 3D-IC integration, hybrid bonding,
and backside power delivery networks — require a coordinate system where **z = 0 is the
bonding plane**, not the substrate bottom. Layers above the bonding plane have positive z;
layers below (backside metal, through-silicon vias, substrate, deep trench structures) have
negative z.

This is a first-class concern in the data model. `z_start_nm` values are signed. The tool
makes no assumption that layers start at zero or above. The bonding plane at z = 0 is
semi-special in the following sense:

- It is a natural reference anchor for the TOML definition
- `z = 0` does not need to be explicitly declared; it is implied by the coordinate system

The Klayout 2.5D script supports negative z coordinates natively via the `zstart` option,
so no special handling is required in the emitter.

**v1 simplification:** The tool does not segregate layers by sign for reporting purposes.
The stack is treated as a monolithic whole. Per-sign reporting is deferred to v2.

### 1.3 Goals

- G1: Parse a TOML stack definition and produce a Klayout layer file (`.lyp`, XML) and/or a 2.5D Ruby script
- G2: Read a Klayout `.lyp` file and generate a TOML stack definition (display properties populated; physical properties empty; names blank — see Section 5.5)
- G3: Read a gks-generated 2.5D Ruby script and extract physical properties into TOML
- G4: Accept both `.lyp` and 2.5D script simultaneously and merge them into a single TOML
- G5: Handle missing or incomplete TOML fields gracefully using document-level defaults and configurable fallbacks
- G6: Emit verbose, human-readable diagnostics: assumptions made, defaults applied, parallel z groups detected

### 1.4 Non-Goals

- NG1: Command line only for v1. GUI only for v2.
- NG2: Parsing of hand-written or programmatic 2.5D Ruby scripts with Ruby variables, loops,
  or conditionals. Only gks-generated scripts (with literal values) are supported as input.
- NG3: Color and stipple management GUI — v2 only.

### 1.5 Success Criteria

- **Full round-trip:** `TOML → .lyp → TOML` is lossless for all display properties
- **Full round-trip:** `TOML → 2.5D script → TOML` is lossless for physical properties
- **Bootstrap round-trip:** `.lyp + 2.5D script → TOML → .lyp + 2.5D script` is lossless
- Round-trip with missing fields converges on stable, identical results
- All core functions have unit test coverage
- End-to-end functional tests for all major use cases

---

## 2. Users, Use Cases & Workflows

### 2.1 Target Users

- **Primary user(s):** Standard Cell Developers and PDK Engineers

### 2.2 Key Use Cases

- **UC1: Generate enablement from scratch**
  - User creates a TOML stack definition
  - Tool emits verbose diagnostics — missing fields, defaults applied, parallel groups detected
  - Output: `.lyp` and/or 2.5D Ruby script

- **UC2: Round-trip modify existing TOML**
  - User edits the TOML stack definition
  - Output: Regenerated `.lyp` and/or 2.5D script

- **UC3: Migrate from `.lyp` only**
  - User provides existing `.lyp` file
  - Output: TOML with display properties populated; physical fields present but empty;
    **names blank** (`.lyp` does not store layer names — user must supply them)
  - User fills in names, `z_start_nm`, `thickness_nm`, `material` in TOML
  - Output: Complete `.lyp` + 2.5D script

- **UC4: Bootstrap from existing `.lyp` + 2.5D script**
  - User provides existing `.lyp` and gks-generated 2.5D script
  - Output: Fully populated TOML (display + physical merged; names from script `name:` option
    where present, blank otherwise)
  - User reviews, fills in any blank names, edits TOML
  - Output: Regenerated `.lyp` + 2.5D script

- **UC5: Validate a stack definition**
  - User runs `gks validate` against a TOML file
  - Output: Tiered diagnostics (error / warning / info) with no file generation

### 2.3 Example Scenarios

*(To be filled in with representative PDK layer examples — e.g., ASAP7 or sky130 —
once Phase 1 is complete.)*

---

## 3. Architecture Overview

### 3.1 Canonical Representation

**The TOML file is the single source of truth.** Both `.lyp` and the 2.5D Ruby script are
generated artifacts. The tool never treats either as a primary input for ongoing work — they
are only sources for the one-time bootstrap (UC4) or migration (UC3) workflows.

```
TOML  ──►  .lyp              (display artifact — DisplayProps only)
      ──►  2.5D Ruby script  (rendering artifact — PhysicalProps only)

.lyp                ──►  TOML  (migration: display populated, physical empty, names blank)
2.5D script         ──►  TOML  (migration: physical populated, display empty)
.lyp + 2.5D script  ──►  TOML  (bootstrap: display + physical merged)
```

### 3.2 High-Level Component Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                            CLI                               │
│       (subcommand dispatch, argument parsing, logging)       │
└────────┬──────────────────────────────────────┬─────────────┘
         │                                       │
         ▼                                       ▼
┌────────────────────┐               ┌──────────────────────────┐
│    io / readers    │               │      io / writers         │
│                    │               │                           │
│  TomlReader        │               │  TomlWriter               │
│  LypReader         │               │  LypWriter                │
│  ScriptReader      │               │  ScriptWriter (2.5D Ruby) │
└────────┬───────────┘               └──────────────┬───────────┘
         │                                           │
         ▼                                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    core / LayerStack IR                      │
│                                                             │
│   LayerStack, LayerEntry, DisplayProps, PhysicalProps       │
│   buildStack(), Validator, Defaulter                        │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 Data Flow

**TOML → outputs (primary flow):**
1. `TomlReader` parses TOML into `std::vector<RawLayer>` (raw, unvalidated)
2. `buildStack()` resolves z coordinates using hybrid accumulation; populates `LayerStack` IR
3. `Validator` runs tiered validation — first pass, reports raw issues
4. `Defaulter` fills missing `DisplayProps` using document-level defaults; logs all substitutions
5. `Validator` runs again — second pass, confirms IR is complete for requested output mode
6. Writers emit requested output format(s)

**Import flow (`.lyp` + optional script → TOML):**
1. `LypReader` parses XML; populates `DisplayProps` from element values; `PhysicalProps = nullopt`;
   `name = ""` (`.lyp` does not store names)
2. If `--script` provided: `ScriptReader` parses `z()` / `zz()` calls; merges physical properties
   into `LayerStack` by `(layer_num, datatype)` key; populates `name` from script `name:` option
   where present
3. `Validator` runs; warns on layers present in script but absent from `.lyp` and vice versa;
   warns on any entries with blank name
4. `TomlWriter` emits TOML; blank names emitted as empty strings with a comment directing the
   user to fill them in

---

## 4. Data Model & Core Abstractions

### 4.1 Domain Concepts

- **LayerStack** — the complete technology layer definition; the top-level IR object
- **LayerEntry** — one logical layer; represents a single `(GDS layer, datatype)` pair
- **DisplayProps** — visual rendering properties consumed by the `.lyp` writer
- **PhysicalProps** — 3D physical properties consumed by the 2.5D Ruby writer
- **RawLayer** — intermediate type holding one parsed TOML layer block, all fields optional,
  prior to IR construction
- **Bonding plane** — `z = 0` in the coordinate system; layers above are positive z (FEOL/BEOL),
  layers below are negative z (backside metal, TSV, substrate)
- **Parallel layer group** — two or more `LayerEntry` objects sharing the same `z_start_nm`,
  representing co-planar process layers realized in the same fabrication step

**Key relationships:**
- A `LayerStack` owns an ordered `std::vector<LayerEntry>`
- Each `LayerEntry` has exactly one `DisplayProps` and zero or one `PhysicalProps`
- `(layer_num, datatype)` is the primary key; duplicates are a hard error
- Multiple entries may share the same `z_start_nm` — this is legal and represents a parallel group
- One `layer_num` commonly has multiple `datatype` values (drawing, label, pin, etc.);
  each `(layer_num, datatype)` pair is a separate `LayerEntry`

### 4.2 The Z Coordinate System

`z_start_nm` is a **signed double**. Negative values are valid and expected for technologies
involving backside structures or wafer bonding.

```
        +z (positive)
         │
         │   BEOL metals, vias
         │   Gate, poly, diffusion
─────────┼─────────────────────  z = 0  (bonding plane)
         │
         │   Backside power delivery
         │   Through-silicon vias
         │   Substrate
         │
        -z (negative)
```

The high-water mark used during z accumulation (Section 5.2) is a signed double, initialized
to 0.0 or to the first explicit `z_start_nm` encountered.

### 4.3 Core Data Structures

```cpp
namespace gks {

// ─── Color ────────────────────────────────────────────────────────────────────

struct Color {
    uint8_t r, g, b;

    static std::expected<Color, std::string> fromHex(std::string_view s);
    // Accepts "#rrggbb" (lyp/TOML format) or "0xrrggbb" (script format)

    std::string toHex() const;    // returns "#rrggbb"  — for .lyp and TOML
    uint32_t    toInt() const;    // returns 0xrrggbb   — for 2.5D script color:/fill:/frame:
};

// ─── DisplayProps ─────────────────────────────────────────────────────────────
//
// Responsibility: Complete visual appearance of a layer in the Klayout viewer.
//                 Maps 1:1 to a <properties> block in the .lyp XML.
//                 All fields required for .lyp output.
//
// .lyp XML element mapping:
//   frame_color      ← <frame-color>       "#rrggbb"
//   fill_color       ← <fill-color>        "#rrggbb"
//   frame_brightness ← <frame-brightness>  int, typically 0
//   fill_brightness  ← <fill-brightness>   int, typically 0
//   dither_pattern   ← <dither-pattern>    "I{n}" — stored as int n; -1 = solid (no element)
//   line_style       ← <line-style>        "L{n}" or empty — stored as int n; -1 = no style
//   valid            ← <valid>             bool
//   visible          ← <visible>           bool
//   transparent      ← <transparent>       bool
//   width            ← <width>             int or empty — stored as optional<int>
//   marked           ← <marked>            bool
//   xfill            ← <xfill>             bool
//   animation        ← <animation>         int (0 = no animation)
//   expanded         ← <expanded>          bool (UI tree state; not exposed in TOML)
//
// Encoding notes:
//   dither_pattern: .lyp stores "I9", "I5" etc. Parse the integer after "I".
//                   Emit as "I{n}". Value -1 means empty element (solid fill).
//   line_style:     .lyp stores "L{n}" or empty element. Parse integer after "L".
//                   Emit as "L{n}" or empty. Value -1 means empty element.
//   source:         NOT stored in DisplayProps. Stored in LayerEntry as
//                   (layer_num, datatype). .lyp <source> format: "layer/dt@1".
//                   Always emit "@1" on write.
//   name:           NOT in DisplayProps. Stored in LayerEntry.name. .lyp <name>
//                   is typically empty — do not rely on it as a source of truth.
//
// TOML exposure:
//   Core fields (fill_color, frame_color, fill_alpha, dither_pattern, line_style,
//   visible, valid, transparent) are exposed in TOML.
//   Rarely-changed fields (frame_brightness, fill_brightness, width, marked,
//   xfill, animation) are NOT exposed in TOML; they are held at default values
//   and round-tripped faithfully through the IR for .lyp fidelity.
//
// Invariants:
//   dither_pattern >= -1
//   line_style >= -1
//   fill_alpha in [0, 255]  (derived from fill_brightness + fill_color; see note below)

// Note on alpha / brightness:
//   The .lyp format does not have a direct alpha field. Klayout uses
//   <fill-brightness> (int, typically 0) combined with the fill color to control
//   transparency. The <transparent> boolean is a separate visibility toggle.
//   For v1, fill_alpha in the TOML is mapped to fill_brightness on write
//   using a simple linear scale. This may be refined in v2 with reference
//   to Klayout source behavior.

struct DisplayProps {
    // ── TOML-exposed fields ──────────────────────────────────────────────────
    Color   fill_color   = {0x80, 0x80, 0x80};
    Color   frame_color  = {0x80, 0x80, 0x80};
    uint8_t fill_alpha   = 128;      // mapped to fill_brightness on .lyp write
    int     dither_pattern = -1;     // -1 = solid; stored as int, emitted as "I{n}"
    int     line_style     = -1;     // -1 = none;  stored as int, emitted as "L{n}" or empty
    bool    visible      = true;
    bool    valid        = true;
    bool    transparent  = false;

    // ── Passthrough fields (not in TOML; round-tripped for .lyp fidelity) ───
    int                  frame_brightness = 0;
    int                  fill_brightness  = 0;
    std::optional<int>   width;           // empty element in .lyp if absent
    bool                 marked    = false;
    bool                 xfill     = false;
    int                  animation = 0;
    bool                 expanded  = false;
};

// ─── PhysicalProps ────────────────────────────────────────────────────────────
//
// Responsibility: 3D physical geometry of a layer in the process stack.
//                 Optional — absent for display-only layers or after .lyp migration.
//
// Invariants:
//   thickness_nm >= 0.0
//   z_start_nm is signed; negative values are valid (backside / below bonding plane)
//
// Notes:
//   z_start_nm is always stored as absolute in the IR regardless of whether it
//   was explicitly provided or computed by hybrid accumulation during TOML parsing.
//
//   layer_expression: if present, ScriptWriter emits a zz() block instead of z().
//   Format: a Klayout DRC boolean expression referencing input() calls,
//   e.g. "input(4,0) + input(5,0)" for a parallel group union.

struct PhysicalProps {
    double      z_start_nm;
    double      thickness_nm;
    std::string material;
    std::optional<std::string> layer_expression;   // triggers zz() emit
};

// ─── LayerEntry ───────────────────────────────────────────────────────────────
//
// Responsibility: Single entry in the layer stack; maps to one TOML [[layer]]
//                 block and one GDS (layer_num, datatype) pair.
//
// Invariants:
//   (layer_num, datatype) unique within a LayerStack.
//   display always populated (defaulted if missing in TOML).
//
// Notes:
//   name may be empty string after .lyp import — .lyp <name> elements are
//   typically blank. The validator warns on any entry with an empty name.
//   physical = nullopt is valid for display-only or not-yet-specified layers.

struct LayerEntry {
    int         layer_num;
    int         datatype;
    std::string name;       // may be "" after .lyp-only import
    std::string purpose;

    DisplayProps                 display;
    std::optional<PhysicalProps> physical;
};

// ─── LayerStack ───────────────────────────────────────────────────────────────
//
// Responsibility: Top-level IR; the complete technology layer definition.
//
// Invariants:
//   No two entries share the same (layer_num, datatype) pair.
//   layers preserves TOML document order; sort-by-z at emit time only.

struct LayerStack {
    std::string             tech_name;
    std::string             version;
    std::vector<LayerEntry> layers;

    struct Defaults {
        std::optional<double>      thickness_nm;
        std::optional<uint8_t>     fill_alpha;
        std::optional<std::string> material;
        std::optional<int>         dither_pattern;
        std::optional<int>         line_style;
    } defaults;
};

// ─── RawLayer ─────────────────────────────────────────────────────────────────
//
// Responsibility: Intermediate type; one parsed [[layer]] TOML block,
//                 all fields optional. Used between TomlReader and buildStack().
//                 Not part of the public API.

struct RawLayer {
    std::optional<int>         layer_num;
    std::optional<int>         datatype;
    std::optional<std::string> name;
    std::optional<std::string> purpose;

    // DisplayProps — TOML-exposed subset
    std::optional<Color>   fill_color;
    std::optional<Color>   frame_color;
    std::optional<uint8_t> fill_alpha;
    std::optional<int>     dither_pattern;
    std::optional<int>     line_style;
    std::optional<bool>    visible;
    std::optional<bool>    valid;
    std::optional<bool>    transparent;

    // PhysicalProps
    std::optional<double>      z_start_nm;     // absent → accumulate
    std::optional<double>      thickness_nm;
    std::optional<std::string> material;
    std::optional<std::string> layer_expression;

    int source_line = 0;   // 1-based TOML line number for diagnostics
};

} // namespace gks
```

### 4.4 .lyp XML Field Reference

Complete field mapping between `.lyp` XML elements and the `gks` IR, derived from
inspection of real `.lyp` files. The `.lyp` root element is `<layer-properties>`;
each layer is a `<properties>` child element.

| XML element          | Type / format        | IR field                     | Notes |
|----------------------|----------------------|------------------------------|-------|
| `<source>`           | `"L/DT@idx"`         | `layer_num`, `datatype`      | Parse L/DT; always emit `@1` |
| `<name>`             | string (often empty) | `LayerEntry.name`            | optional; populate when present; blank is valid |
| `<fill-color>`       | `"#rrggbb"`          | `display.fill_color`         | |
| `<frame-color>`      | `"#rrggbb"`          | `display.frame_color`        | |
| `<fill-brightness>`  | int                  | `display.fill_brightness`    | Typically 0; passthrough |
| `<frame-brightness>` | int                  | `display.frame_brightness`   | Typically 0; passthrough |
| `<dither-pattern>`   | `"I{n}"`             | `display.dither_pattern`     | Parse int after "I"; -1 if element empty |
| `<line-style>`       | `"L{n}"` or empty    | `display.line_style`         | Parse int after "L"; -1 if element empty |
| `<valid>`            | bool string          | `display.valid`              | |
| `<visible>`          | bool string          | `display.visible`            | |
| `<transparent>`      | bool string          | `display.transparent`        | |
| `<width>`            | int or empty         | `display.width`              | `nullopt` if element empty |
| `<marked>`           | bool string          | `display.marked`             | Passthrough |
| `<xfill>`            | bool string          | `display.xfill`              | Passthrough |
| `<animation>`        | int                  | `display.animation`          | Passthrough |
| `<expanded>`         | bool string          | `display.expanded`           | UI tree state; passthrough |

**Elements with no IR counterpart (emit fixed values on write):**
- None — all elements in the observed schema are mapped above.

**LypWriter element order:** Emit elements in the order shown in the table above to produce
output identical in structure to Klayout-generated files.

### 4.5 TOML Stack Definition Format

This is the canonical file format. TOML-exposed `DisplayProps` fields are the subset
listed in Section 4.3. Passthrough fields (`frame_brightness`, `fill_brightness`, `width`,
`marked`, `xfill`, `animation`) are not present in TOML; they default to 0/false/absent
on fresh TOML-originated layers and are preserved in the IR when round-tripping through
`.lyp`.

`dither_pattern` and `line_style` are stored as integers in TOML (e.g. `dither_pattern = 9`),
corresponding to the `n` in Klayout's `"I{n}"` / `"L{n}"` notation.

```toml
[stack]
tech_name = "ASAP7"
version   = "1.0.0"

[stack.defaults]
thickness_nm    = 36.0
fill_alpha      = 128
material        = "dielectric"
dither_pattern  = 9     # I9 in Klayout notation
line_style      = -1    # no line style

# ── Backside layers (negative z) ──────────────────────────────────────────────

[[layer]]
name           = "substrate"
layer_num      = 0
datatype       = 0
purpose        = "drawing"
fill_color     = "#888888"
frame_color    = "#444444"
dither_pattern = 9
z_start_nm     = -300.0      # explicit: below bonding plane
thickness_nm   = 300.0
material       = "silicon"

# ── Front-side layers (positive z) ────────────────────────────────────────────

[[layer]]
name         = "diffusion"
layer_num    = 1
datatype     = 0
purpose      = "drawing"
fill_color   = "#FFAA00"
frame_color  = "#CC8800"
# z_start_nm omitted → accumulate from high_water
thickness_nm = 50.0
material     = "silicon"

# Parallel group: epi_contact and gate_contact share z_start_nm
[[layer]]
name         = "epi_contact"
layer_num    = 4
datatype     = 0
purpose      = "drawing"
fill_color   = "#FF8800"
frame_color  = "#CC6600"
z_start_nm   = 105.0          # explicit: parallel group start
thickness_nm = 45.0
material     = "tungsten"

[[layer]]
name         = "gate_contact"
layer_num    = 5
datatype     = 0
purpose      = "drawing"
fill_color   = "#AA4400"
frame_color  = "#882200"
z_start_nm   = 105.0          # explicit: same z → parallel group
thickness_nm = 45.0
material     = "tungsten"
# layer_expression = "input(4,0) + input(5,0)"  # optional: emit zz() block

[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
purpose      = "drawing"
fill_color   = "#0055FF"
frame_color  = "#0033CC"
# z_start_nm omitted → accumulate from high_water after parallel group
thickness_nm = 36.0
material     = "metal"
```

---

## 5. Algorithms & Pipelines

### 5.1 Algorithm List

- A1: `buildStack`   — `RawLayer` vector → `LayerStack` IR with z resolution
- A2: `validateStack` — tiered validation of a `LayerStack` IR
- A3: `applyDefaults` — fill missing fields using document-level defaults
- A4: `writeLyp`     — `LayerStack` IR → Klayout `.lyp` XML
- A5: `writeScript`  — `LayerStack` IR → Klayout 2.5D Ruby script (`z()` / `zz()`)
- A6: `readLyp`      — Klayout `.lyp` XML → `LayerStack` (PhysicalProps absent, names blank)
- A7: `readScript`   — gks-generated 2.5D Ruby script → `std::vector<RawLayer>` (physical only)

### 5.2 A1: buildStack — Hybrid Z Accumulation

**Purpose:** Resolve `z_start_nm` for all layers using a hybrid accumulation model.

**The hybrid z model:**

> If `z_start_nm` is **absent** in a `RawLayer`, the layer's z_start is set to the current
> **high-water mark** (maximum `z_start + thickness` seen so far).
> If `z_start_nm` is **explicitly provided**, it is used directly.

After each layer:
```
high_water = max(high_water, z_start + thickness_nm)
```

`high_water` is a signed double; negative values are valid for backside stacks.

**Pseudo-code:**

```text
function buildStack(raw: vector<RawLayer>, defaults: Defaults) -> LayerStack:

    stack        = LayerStack{}
    high_water   = 0.0
    hw_initialized = false

    for row in raw:
        entry.layer_num = require(row.layer_num, row.source_line)
        entry.datatype  = row.datatype ?? 0
        entry.name      = row.name ?? ""
        entry.purpose   = row.purpose ?? "drawing"
        entry.display   = resolveDisplay(row, defaults)   // A3

        has_physical = row has any of {z_start_nm, thickness_nm, material}

        if has_physical:
            thickness = row.thickness_nm ?? defaults.thickness_nm ?? 0.0

            if row.z_start_nm is present:
                z_start = row.z_start_nm
                if hw_initialized:
                    if z_start < high_water - epsilon:
                        WARN "[line N] '{name}': z_start={z_start}nm below high_water={high_water}nm — possible burial"
                    elif z_start > high_water + epsilon:
                        WARN "[line N] '{name}': {z_start - high_water}nm gap above high_water"
            else:
                z_start = hw_initialized ? high_water : 0.0

            hw_initialized = true
            high_water = max(high_water, z_start + thickness)

            entry.physical = PhysicalProps{
                z_start_nm       = z_start,
                thickness_nm     = thickness,
                material         = row.material ?? defaults.material ?? "",
                layer_expression = row.layer_expression
            }
        else:
            entry.physical = nullopt

        stack.layers.push_back(entry)

    return stack
```

**Diagnostic output (G6):**

```
INFO  'substrate'    : z=[-300.0, 0.0] nm   (explicit)
INFO  'diffusion'    : z=[0.0, 50.0] nm     (accumulated)
INFO  'epi_contact'  : z=[105.0, 150.0] nm  (explicit — parallel group start)
INFO  'gate_contact' : z=[105.0, 150.0] nm  (explicit — parallel group {epi_contact, gate_contact})
INFO  'm0'           : z=[150.0, 186.0] nm  (accumulated from high_water=150.0)
WARN  'via1'         : 2 entries with blank name — fill in before generating output

SUMMARY  Parallel groups detected: 1  ({epi_contact, gate_contact} @ z=[105.0, 150.0] nm)
SUMMARY  Blank names: 2 layer(s) require names before output is complete
```

**Edge cases and testing:** See Section 5.2 of v0.3.0 spec — unchanged.

### 5.3 A2: validateStack — Tiered Validation

```
validate_identity():
    - No duplicate (layer_num, datatype) pairs                   [error]
    - name non-empty for all entries                             [info]
    - datatype defaults to 0 if absent                          [info]

validate_for_lyp():
    - All DisplayProps TOML-exposed fields present and in range  [error]
    - fill_alpha in [0, 255]                                     [error]
    - dither_pattern >= -1                                       [error]

validate_for_3d():
    - All entries have PhysicalProps present                     [error]
    - thickness_nm >= 0.0                                        [error]
    - Overlapping z ranges with same material (non-parallel)     [warning]

validate_full():
    - All of the above
```

### 5.4 A5: writeScript — 2.5D Ruby Script Writer

**z() emit (standard layer):**

```ruby
z(input(6, 0), zstart: 150.0.nm, height: 36.0.nm,
  fill: 0x0055ff, frame: 0x0033cc, name: "m0")
```

**zz() emit (parallel group / layer_expression present):**

When `layer_expression` is set on a layer, the writer emits a `zz()` block:

```ruby
zz(name: "contacts", fill: 0xff8800, frame: 0xcc6600) do
  z(input(4,0), zstart: 105.0.nm, height: 45.0.nm)
  z(input(5,0), zstart: 105.0.nm, height: 45.0.nm)
end
```

**Writer rules:**
- Emit all z values in nm with `.nm` suffix (not `.um`) for clarity
- Emit `fill:` and `frame:` as hex integers (`0xrrggbb`)
- Emit `name:` from `LayerEntry.name`; omit if name is empty
- Layers without `PhysicalProps` are skipped with an INFO comment in the script header
- Sort layers by `z_start_nm` ascending before emit (do not modify IR order)
- Negative `z_start_nm` emits correctly: `zstart: -300.0.nm`

**Script file header (gks-generated marker for ScriptReader recognition):**

```ruby
# Generated by genKlayoutStack (gks) v{version}
# Tech: {tech_name}  Version: {stack_version}
# Source: {input_toml_filename}
# DO NOT EDIT — regenerate from TOML source
#
# Layers without physical properties are omitted from this script.
```

### 5.5 A6: readLyp — .lyp XML Reader

**Source field parsing:**

The `<source>` element has format `"layer_num/datatype@layout_index"`, e.g. `"1/0@1"`.
Parse by splitting on `/` and `@`:

```text
source = "1/0@1"
layer_num = 1
datatype  = 0
layout_idx = 1   // always 1; ignored on read; always emit @1 on write
```

**Name handling:**

The `<name>` element is typically empty in real `.lyp` files. On import, store whatever
value is present (including empty string). The Validator warns on blank names after import.

**Dither pattern and line style parsing:**

```text
<dither-pattern>I9</dither-pattern>  →  dither_pattern = 9
<dither-pattern/>                    →  dither_pattern = -1  (solid)
<line-style/>                        →  line_style = -1      (none)
<line-style>L3</line-style>          →  line_style = 3
```

**Bool element parsing:**

Klayout emits `"true"` / `"false"` strings. Parse case-insensitively.

### 5.6 A7: readScript — 2.5D Ruby Script Parser

**Scope:** gks-generated scripts with literal numeric values only (NG2). Scripts with Ruby
variables or control flow in argument positions emit a WARNING and are parsed best-effort.

**Parsing targets:**

```ruby
# Standard z() call
z(input(L, DT), zstart: V.nm, height: V.nm, fill: 0xRRGGBB, frame: 0xRRGGBB, name: "N")

# zz() group block
zz(name: "N", fill: 0xRRGGBB, frame: 0xRRGGBB) do
  z(input(L, DT), zstart: V.nm, height: V.nm)
  z(input(L, DT), zstart: V.nm, height: V.nm)
end
```

**Unit suffix handling:**

| Suffix | Conversion |
|--------|------------|
| `.nm`  | value × 1  |
| `.um`  | value × 1000 |
| `.mm`  | value × 1_000_000 |
| none   | assume nm, emit INFO |

**Keyword argument extraction:** Match named arguments by key string, not by position.
Argument order in `z()` calls is not guaranteed.

**zz() block handling:**

When a `zz()` block is encountered:
1. Extract the `name:` and display options from the `zz()` argument list
2. Parse each inner `z()` call as a normal layer with its own `input(L, DT)`
3. Set `layer_expression` on the first member to the DRC boolean union of all members'
   `input()` calls, e.g. `"input(4,0) + input(5,0)"`
4. The `zz()` group name is stored as a comment annotation (not a first-class field in v1)

**Merge behavior (UC4):** Match by `(layer_num, datatype)`. Unmatched script entries → WARNING.
Display color values from script (`fill:`, `frame:`) override `.lyp` values only if the
user passes `--prefer-script-colors` flag; default is to preserve `.lyp` colors.

---

## 6. CLI Design

### 6.1 Subcommands

```
gks <subcommand> [options]

Subcommands:
  import     Create a TOML stack definition from existing .lyp and/or 2.5D script
  generate   Generate .lyp and/or 2.5D script from a TOML stack definition
  validate   Validate a TOML stack definition without generating output
```

### 6.2 import

```
gks import [--lyp <file>] [--script <file>] -o <output.toml>
           [--prefer-script-colors] [--verbose]

At least one of --lyp or --script must be provided.
```

```bash
gks import --lyp existing.lyp -o stack.toml
gks import --lyp existing.lyp --script existing.rb -o stack.toml
gks import --lyp existing.lyp --script existing.rb --prefer-script-colors -o stack.toml
```

### 6.3 generate

```
gks generate <stack.toml> [--lyp <file>] [--script <file>] [--verbose]

At least one of --lyp or --script must be provided.
```

```bash
gks generate stack.toml --lyp out.lyp --script out.rb
gks generate stack.toml --lyp out.lyp             # display only; no PhysicalProps required
gks generate stack.toml --lyp new.lyp --script new.rb --verbose
```

### 6.4 validate

```
gks validate <stack.toml> [--for lyp|3d|full] [--verbose]
```

### 6.5 Standard Daily Workflow

```bash
# One-time bootstrap
gks import --lyp orig.lyp --script orig.rb -o stack.toml
# ... fill in blank names, review TOML ...

# Daily iteration
gks generate stack.toml --lyp new.lyp --script new.rb --verbose
```

---

## 7. Codebase Layout & Module Boundaries

### 7.1 Directory Structure

```text
genKlayoutStack/
  CMakeLists.txt
  include/
    gks/
      core/
        LayerStack.hpp       ← all core types + buildStack declaration
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
    default.lyp                  ← reference .lyp (real file, used in LypReader tests)
    asap7_stack.toml             ← reference stack for integration tests
    asap7_25d.rb
  docs/
    PROJECT_SPEC.md
```

### 7.2 Modules & Boundaries

- **Module `core`** — `LayerStack` types, `buildStack`, `Validator`, `Defaulter`; stdlib only;
  must not depend on CLI, io, XML, TOML, or Ruby parsing
- **Module `io`** — all readers and writers; depends on `core`; must not depend on CLI
- **Module `cli`** — subcommand dispatch, argument parsing, logging; depends on `core`, `io`

### 7.3 Naming Conventions

- Namespace: `gks`
- Files: `PascalCase.hpp` / `PascalCase.cpp` for classes; `camelCase.cpp` for free functions
- Error handling: `std::expected<T, GksError>` for recoverable errors; exceptions at IO
  boundary for unrecoverable failures (malformed XML, unreadable file)

---

## 8. External Dependencies & Tooling

### 8.1 Languages & Versions

- **Primary language:** C++20
- **Build system:** CMake + Ninja
- **Supported platforms:** macOS 14+, Linux x86_64

### 8.2 Libraries

- **toml++** (`tomlplusplus`) — TOML parsing/writing; FetchContent; header-only C++17/20
- **pugixml** — `.lyp` XML parsing/writing; FetchContent
- **GoogleTest** — unit and integration testing; FetchContent

### 8.3 Testing & QA

- Unit tests: `tests/unit/` via `ctest`
- Integration tests: `tests/integration/` with fixtures including `default.lyp`
- Coverage target: 80%+ on `core/` and `io/`

---

## 9. LLM Collaboration Plan

### 9.1 Prompt Cookbook

- "Given Section 4.3 structs, implement `LypReader` per the field mapping table in Section 4.4."
- "Given Section 4.3 structs and Section 5.4, implement `ScriptWriter` with z()/zz() emit."
- "Given Section 5.6, implement `ScriptReader` with unit suffix handling and zz() block support."
- "Given Section 5.2 pseudocode, implement `buildStack()` in `src/core/buildStack.cpp`."
- "Propose GoogleTests for `LypReader` using `fixtures/default.lyp` as the reference input."
- "Implement `cmd_import.cpp` per CLI design in Section 6.2."

---

## 10. Roadmap & Milestones

### 10.1 Phases

1. **Phase 0 – Skeleton**
   Repo layout, CMake, blank modules, `ctest` smoke test, `fixtures/` directory with `default.lyp`

2. **Phase 1 – Core Data Model**
   All types in `LayerStack.hpp`; `buildStack()` with signed z accumulation; unit tests for
   all edge cases: sequential, negative z, parallel groups, burial/gap warnings, blank names

3. **Phase 2 – Validation & Defaulting**
   Tiered `Validator`; `Defaulter` with document-level defaults; unit tests

4. **Phase 3 – TOML I/O**
   `TomlReader` → `buildStack()`; `TomlWriter` (always explicit z); round-trip tests

5. **Phase 4 – .lyp I/O**
   `LypReader` per Section 4.4 field mapping; `LypWriter`; round-trip tests using
   `default.lyp` as fixture; verify passthrough field fidelity

6. **Phase 5 – 2.5D Script I/O**
   `ScriptWriter` with `z()` / `zz()` emit; `ScriptReader` with unit suffix handling,
   `zz()` block parsing, merge-by-key behavior; round-trip tests; negative z fixture;
   parallel group fixture

7. **Phase 6 – CLI**
   `import`, `generate`, `validate` subcommands; end-to-end functional tests for all UCs;
   `--prefer-script-colors` flag

---

## 11. Open Questions, Risks & Parking Lot

### 11.1 Open Questions

- ~~OQ2: Stipple/line style as integer vs. string?~~
  **Closed.** Store as integer; emit as `I{n}` / `L{n}` in `.lyp`; store as plain integer
  in TOML. Confirmed by `.lyp` schema analysis.

- ~~OQ4: zz() support in v1 or v2?~~
  **Closed.** `zz()` supported in v1, both as output (`ScriptWriter`) and input
  (`ScriptReader`). `layer_expression` field reserved in IR for this purpose.

- ~~OQ5: Report layers by z sign (above/below bonding plane)?~~
  **Closed for v1.** Stack treated as monolithic. Sign-aware reporting deferred to v2.
  (Note: multi-wafer bonding with more than two bonded layers is a future problem.)

- OQ1: `TomlWriter` output — always emit explicit absolute `z_start_nm` for all layers,
  or attempt to reproduce sparse/hybrid input form? Current leaning: always emit explicit
  absolute for unambiguity.

- OQ3: `like:` option on `ScriptReader` import — currently noted but ignored (`.lyp`
  colors take precedence). `--prefer-script-colors` flag allows override. Sufficient for v1?

### 11.2 Risks

- **Script parser fragility:** Only gks-generated scripts supported as input (NG2).
  Hand-written scripts with Ruby variables emit WARNING and parse best-effort.
- **fill_alpha / fill_brightness mapping:** Klayout's alpha model is not a simple linear
  mapping. May need empirical calibration in Phase 4. Flag for early testing.
- **Negative z in Klayout 2.5D:** Verify `zstart: -300.0.nm` is accepted by the Klayout
  2.5D engine across supported versions. Add to Phase 5 integration test fixture.

### 11.3 Parking Lot

- Color and stipple chooser GUI (FG1 — v2)
- Sign-aware z reporting for multi-wafer bonding scenarios (v2)
- `layer_expression` / `zz()` group name as a first-class TOML field (currently annotation only)
- Per-substack TOML include for large PDKs

---

## 12. Appendices

### 12.1 Glossary

- **Bonding plane** — `z = 0`; reference plane for wafer-bonded 3D-IC technologies
- **Canonical representation** — the TOML stack definition file
- **Design layer** — a GDS `(layer_num, datatype)` pair
- **Dither pattern** — Klayout stipple fill pattern, referenced by integer index n (`I{n}` in `.lyp`)
- **High-water mark** — maximum `z_start_nm + thickness_nm` seen so far; signed; default z_start for next accumulated layer
- **Parallel layer group** — two or more co-planar design layers sharing `z_start_nm`
- **Process layer** — a physical structure that may correspond to multiple design layers
- **.lyp** — Klayout layer properties XML file; display properties only; no layer names in practice
- **2.5D script** — Klayout Ruby DRC macro using `z()` / `zz()` calls to extrude layers into 3D

### 12.2 References

- Klayout 2.5D scripting API: https://www.klayout.de/doc-qt5/about/25d_view.html
- Klayout DRC scripting (parent of 2.5D): https://www.klayout.de/doc/programming/drc_ref.html
- toml++ library: https://github.com/marzer/tomlplusplus
- pugixml: https://pugixml.org

### 12.3 Design Alternatives Considered

- **CSV as canonical format:** Rejected — no document-level metadata, no grouping, no comments,
  parallel groups implicit only.
- **Custom DSL:** Considered for v2 if adoption warrants it. The `io` abstraction supports
  adding a new reader behind the same `LayerStack` IR.
- **Embedding 3D metadata in `.lyp` XML:** Rejected — undocumented parser tolerance; conflates
  rendering artifact with technology definition.
- **Integer z_order ranking:** Rejected — cannot represent parallel co-planar layers without
  arbitrary tie-breaking. Absolute signed z is the correct model.
- **Forced explicit z_start_nm for all layers:** Rejected — hybrid accumulation (Section 5.2)
  gives the right default with explicit override only where geometry is non-trivial.
