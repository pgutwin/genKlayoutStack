# Project: genKlayoutStack

## 0. Meta & Status

- **Owner:** Paul Gutwin
- **Doc status:** Draft
- **Last updated:** <2026-03-26>
- **Change log:**
  - <2026-03-20> – Initial draft
  - <2026-03-25> – Filled in architecture, data model, and z-ordering decisions from design review
  - <2026-03-26> – Canonical format changed from CSV to TOML; CLI subcommand design added;
                    UC4 (bootstrap from existing artifacts) added; 2.5D script import revised
                    to parse z() calls directly; negative Z / bonding plane model added throughout

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
- The tool can report the stack split by sign (above-bond vs. below-bond) as a diagnostic
- `z = 0` does not need to be explicitly declared; it is implied by the coordinate system

The Klayout 2.5D script itself supports negative z coordinates natively via the `zstart`
option, so no special handling is required in the emitter.

### 1.3 Goals

- G1: Parse a TOML stack definition and produce a Klayout layer file (`.lyp`, XML) and/or a 2.5D Ruby script
- G2: Read a Klayout `.lyp` file and generate a TOML stack definition (display properties populated; physical properties empty)
- G3: Read a gks-generated 2.5D Ruby script and extract physical properties (z, thickness, material, color) into TOML
- G4: Accept both `.lyp` and 2.5D script simultaneously and merge them into a single TOML
- G5: Handle missing or incomplete TOML fields gracefully using document-level defaults and configurable fallbacks
- G6: Emit verbose, human-readable diagnostics: assumptions made, defaults applied, parallel z groups detected, bonding plane crossing flagged
- FG1: Future goal — v2 color and stipple chooser GUI

### 1.4 Non-Goals

- NG1: Command line only for v1. GUI only for v2.
- NG2: Parsing of hand-written or programmatic 2.5D Ruby scripts with Ruby variables, loops,
  or conditionals. Only gks-generated scripts (with literal values) are supported as input.

### 1.5 Success Criteria

- **Full round-trip:** `TOML → .lyp → TOML` is lossless for display properties
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
  - Step 1: User creates a TOML stack definition
  - Step 2: Tool emits verbose diagnostics — missing fields, defaults applied, parallel groups detected, bonding plane crossing reported
  - Output: `.lyp` and/or 2.5D Ruby script

- **UC2: Round-trip modify existing TOML**
  - Step 1: User edits the TOML stack definition
  - Output: Regenerated `.lyp` and/or 2.5D script

- **UC3: Migrate from `.lyp` only**
  - Step 1: User provides existing `.lyp` file
  - Output: TOML with display properties populated; physical property fields present but empty
  - Step 2: User fills in `z_start_nm`, `thickness_nm`, `material` in TOML
  - Output: Complete `.lyp` + 2.5D script

- **UC4: Bootstrap from existing `.lyp` + 2.5D script**
  - Step 1: User provides existing `.lyp` and gks-generated 2.5D script
  - Output: Fully populated TOML (display + physical properties merged from both files)
  - Step 2: User reviews and edits TOML
  - Output: Regenerated `.lyp` + 2.5D script

- **UC5: Validate a stack definition**
  - Step 1: User runs `gks validate` against a TOML file
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

.lyp                 ──►  TOML  (migration: display populated, physical empty)
2.5D script          ──►  TOML  (migration: physical populated, display empty)
.lyp + 2.5D script   ──►  TOML  (bootstrap: display + physical merged)
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
3. `Validator` runs tiered validation (identity, display, physical) — first pass, reports raw issues
4. `Defaulter` fills missing `DisplayProps` using document-level defaults and configured fallbacks; logs all substitutions
5. `Validator` runs again — second pass, confirms IR is complete for requested output mode
6. Writers emit requested output format(s)

**Import flow (`.lyp` + optional script → TOML):**
1. `LypReader` parses XML into `LayerStack`; `PhysicalProps = nullopt` for all entries
2. If `--script` provided: `ScriptReader` parses `z()` / `zz()` call sites, extracts physical
   properties, merges into `LayerStack` by `(layer_num, datatype)` key
3. `Validator` runs identity + display validation; warns on any layers present in script but
   absent from `.lyp` and vice versa
4. `TomlWriter` emits TOML; physical property fields left empty where not resolved

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

The high-water mark used during z accumulation (Section 5.2) is signed and can be negative.
The tool does not enforce any particular layer ordering relative to the bonding plane; it is
the user's responsibility to define the stack correctly. The tool will report a diagnostic
listing all layers that cross or touch `z = 0` to assist with review.

### 4.3 Core Data Structures

```cpp
namespace gks {

// ─── Color ────────────────────────────────────────────────────────────────────

struct Color {
    uint8_t r, g, b;

    // Parse from hex string "#RRGGBB" or "0xRRGGBB"
    static std::expected<Color, std::string> fromHex(std::string_view s);
    std::string toHex() const;  // returns "#RRGGBB"
};

// ─── DisplayProps ─────────────────────────────────────────────────────────────
//
// Responsibility: Visual appearance of a layer in the Klayout viewer.
//                 All fields required for .lyp output.
//
// Invariants:
//   fill_alpha in [0, 255]
//   stipple_index >= -1  (-1 = solid fill)
//
// Notes:
//   stipple_index and line_style reference Klayout's internal pattern library
//   by index. The tool does not embed pattern definitions.

struct DisplayProps {
    Color   fill_color;
    Color   frame_color;
    uint8_t fill_alpha    = 128;
    int     stipple_index = -1;     // -1 = solid fill
    int     line_style    = 0;
    bool    visible       = true;
    bool    valid         = true;
};

// ─── PhysicalProps ────────────────────────────────────────────────────────────
//
// Responsibility: 3D physical geometry of a layer in the process stack.
//                 Optional — absent for display-only layers or after
//                 .lyp-only migration.
//
// Invariants:
//   thickness_nm >= 0.0
//   z_start_nm is signed; negative values are valid (backside / below bonding plane)
//
// Notes:
//   z_start_nm is always stored as absolute in the IR regardless of whether it
//   was explicitly provided or computed by hybrid accumulation during TOML parsing.
//
//   layer_expression: optional DRC boolean expression for parallel process layers
//   rendered as a union, e.g. "input(4,0) + input(5,0)".
//   When present, the ScriptWriter emits a zz() block instead of a z() call.

struct PhysicalProps {
    double      z_start_nm;
    double      thickness_nm;
    std::string material;                          // "metal", "dielectric", "poly", etc.
    std::optional<std::string> layer_expression;   // advanced: zz() boolean union
};

// ─── LayerEntry ───────────────────────────────────────────────────────────────
//
// Responsibility: Single entry in the layer stack; maps to one TOML [[layer]]
//                 block and one GDS (layer_num, datatype) pair.
//
// Invariants:
//   (layer_num, datatype) is unique within a LayerStack.
//   display is always populated (defaulted if missing in TOML).
//
// Notes:
//   physical = nullopt is valid and common for display-only layers or layers
//   imported from .lyp without matching script data.

struct LayerEntry {
    int         layer_num;
    int         datatype;
    std::string name;
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
//   layers preserves TOML document order; sort-by-z occurs only at emit time.
//
// Notes:
//   defaults: document-level defaults applied by Defaulter before IR completion.
//   Passed by const reference to Validator and all writers after construction.

struct LayerStack {
    std::string             tech_name;
    std::string             version;
    std::vector<LayerEntry> layers;

    // Document-level defaults (from [stack.defaults] in TOML)
    struct Defaults {
        std::optional<double>      thickness_nm;
        std::optional<double>      fill_alpha;
        std::optional<std::string> material;
        std::optional<int>         stipple_index;
        std::optional<int>         line_style;
    } defaults;
};

// ─── RawLayer ─────────────────────────────────────────────────────────────────
//
// Responsibility: Intermediate type; one parsed [[layer]] block from TOML,
//                 all fields optional. Used between TomlReader and buildStack().
//                 Not part of the public API.
//
// Notes:
//   z_start_nm absent → compute by accumulation in buildStack().
//   z_start_nm present → use as explicit absolute override.
//   source_line: 1-based TOML line number for diagnostics.

struct RawLayer {
    std::optional<int>         layer_num;
    std::optional<int>         datatype;
    std::optional<std::string> name;
    std::optional<std::string> purpose;

    // DisplayProps
    std::optional<Color>   fill_color;
    std::optional<Color>   frame_color;
    std::optional<uint8_t> fill_alpha;
    std::optional<int>     stipple_index;
    std::optional<int>     line_style;
    std::optional<bool>    visible;
    std::optional<bool>    valid;

    // PhysicalProps
    std::optional<double>      z_start_nm;     // absent = accumulate
    std::optional<double>      thickness_nm;
    std::optional<std::string> material;
    std::optional<std::string> layer_expression;

    int source_line = 0;
};

} // namespace gks
```

### 4.4 TOML Stack Definition Format

This is the canonical file format. All `[stack.defaults]` fields are optional. Per-layer
`z_start_nm` is optional; omitting it triggers accumulation (see Section 5.2).

```toml
[stack]
tech_name = "ASAP7"
version   = "1.0.0"

[stack.defaults]
thickness_nm  = 36.0
fill_alpha    = 128
material      = "dielectric"
stipple_index = -1
line_style    = 0

# ── Backside layers (negative z) ──────────────────────────────────────────────

[[layer]]
name       = "substrate"
layer_num  = 0
datatype   = 0
purpose    = "drawing"
fill_color  = "#888888"
frame_color = "#444444"
z_start_nm  = -300.0      # explicit: bottom of substrate below bonding plane
thickness_nm = 300.0
material   = "silicon"

[[layer]]
name       = "backside_m1"
layer_num  = 60
datatype   = 0
purpose    = "drawing"
fill_color  = "#0044FF"
frame_color = "#0022AA"
z_start_nm  = -80.0
thickness_nm = 36.0
material   = "metal"

# ── Bonding plane z = 0 ────────────────────────────────────────────────────────
# (no explicit layer entry needed; coordinate system reference only)

# ── Front-side layers (positive z) ────────────────────────────────────────────

[[layer]]
name       = "diffusion"
layer_num  = 1
datatype   = 0
purpose    = "drawing"
fill_color  = "#FFAA00"
frame_color = "#CC8800"
# z_start_nm omitted → accumulate from high_water
thickness_nm = 50.0
material   = "silicon"

# Parallel group: epi_contact and gate_contact share z_start_nm
[[layer]]
name       = "epi_contact"
layer_num  = 4
datatype   = 0
purpose    = "drawing"
fill_color  = "#FF8800"
frame_color = "#CC6600"
z_start_nm  = 105.0      # explicit: parallel group start
thickness_nm = 45.0
material   = "tungsten"

[[layer]]
name       = "gate_contact"
layer_num  = 5
datatype   = 0
purpose    = "drawing"
fill_color  = "#AA4400"
frame_color = "#882200"
z_start_nm  = 105.0      # explicit: same z as epi_contact → parallel group
thickness_nm = 45.0
material   = "tungsten"

[[layer]]
name       = "m0"
layer_num  = 6
datatype   = 0
purpose    = "drawing"
fill_color  = "#0055FF"
frame_color = "#0033CC"
# z_start_nm omitted → accumulate from high_water after parallel group
thickness_nm = 36.0
material   = "metal"
```

---

## 5. Algorithms & Pipelines

### 5.1 Algorithm List

- A1: `buildStack` — `RawLayer` vector → `LayerStack` IR with z resolution
- A2: `validateStack` — tiered validation of a `LayerStack` IR
- A3: `applyDefaults` — fill missing fields using document-level and configured defaults
- A4: `writeLyp` — `LayerStack` IR → Klayout `.lyp` XML
- A5: `writeScript` — `LayerStack` IR → Klayout 2.5D Ruby script
- A6: `readLyp` — Klayout `.lyp` XML → `LayerStack` (PhysicalProps absent)
- A7: `readScript` — gks-generated 2.5D Ruby script → `std::vector<RawLayer>` (physical only)

### 5.2 A1: buildStack — Hybrid Z Accumulation

**Purpose:** Resolve `z_start_nm` for all layers, converting sparse user input into fully
absolute `PhysicalProps` in the IR.

**The hybrid z model:**

> If `z_start_nm` is **absent** in a `RawLayer`, the layer's z_start is set to the current
> **high-water mark** — the maximum `z_start + thickness` seen so far. If `z_start_nm` is
> **explicitly provided**, it is used directly.

The high-water mark is a signed double, initialized to the `z_start_nm` of the first layer
that provides one, or `0.0` if none do. After each layer:

```
high_water = max(high_water, z_start + thickness_nm)
```

This correctly handles stacks that are entirely negative z, entirely positive z, or span the
bonding plane.

**Inputs:** `std::vector<RawLayer>`, document-level `Defaults`

**Outputs:** `LayerStack` with all present `PhysicalProps.z_start_nm` populated as absolute values

**Pseudo-code:**

```text
function buildStack(raw: vector<RawLayer>, defaults: Defaults) -> LayerStack:

    stack = LayerStack{}
    high_water = 0.0
    hw_initialized = false

    for row in raw:
        entry = LayerEntry{}
        // identity
        entry.layer_num = require(row.layer_num, row.source_line)
        entry.datatype  = row.datatype ?? 0
        entry.name      = require(row.name, row.source_line)
        entry.purpose   = row.purpose ?? "drawing"
        entry.display   = resolveDisplay(row, defaults)  // A3

        has_physical = row has any of {z_start_nm, thickness_nm, material}

        if has_physical:
            thickness = row.thickness_nm
                        ?? defaults.thickness_nm
                        ?? 0.0

            if row.z_start_nm is present:
                z_start = row.z_start_nm
                if hw_initialized:
                    if z_start < high_water - epsilon:
                        WARN "[line N] '{name}': z_start={z_start} nm is below "
                             "high_water={high_water} nm — possible unintentional burial"
                    elif z_start > high_water + epsilon:
                        WARN "[line N] '{name}': z_start={z_start} nm leaves "
                             "{z_start - high_water} nm gap above high_water"
            else:
                z_start = hw_initialized ? high_water : 0.0

            if not hw_initialized:
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

**Diagnostic output (G6):** One INFO line per layer with physical data:

```
INFO  'substrate'    : z=[-300.0, 0.0] nm    (explicit) [BELOW bonding plane]
INFO  'backside_m1'  : z=[-80.0, -44.0] nm   (explicit) [BELOW bonding plane]
INFO  'diffusion'    : z=[0.0, 50.0] nm       (accumulated) [ABOVE bonding plane]
INFO  'epi_contact'  : z=[105.0, 150.0] nm    (explicit — parallel group start)
INFO  'gate_contact' : z=[105.0, 150.0] nm    (explicit — parallel group {epi_contact, gate_contact})
INFO  'm0'           : z=[150.0, 186.0] nm    (accumulated from high_water=150.0)

SUMMARY  Bonding plane (z=0): 2 layer(s) below, 4 layer(s) above
SUMMARY  Parallel groups: 1 group(s) detected — {epi_contact, gate_contact} at z=[105.0, 150.0] nm
```

**Edge cases:**
- `z_start_nm` explicit but below `high_water` → WARN (burial)
- `z_start_nm` explicit but above `high_water` → WARN (gap)
- `thickness_nm` absent and no default → use 0.0, emit INFO (2D marker layer)
- No physical fields at all on any layer → all `physical = nullopt`; display-only stack
- Stack entirely negative z (backside-only file) → supported; `high_water` stays negative

**Testing strategy:**
- Unit: sequential stack accumulates correctly from arbitrary start z
- Unit: negative z stack — substrate at -300, accumulates upward through bonding plane
- Unit: parallel group — two explicit same-z rows, `high_water` advances past both
- Unit: burial warning triggered when explicit z_start < high_water
- Unit: gap warning triggered when explicit z_start > high_water
- Unit: document-level default thickness applied when per-layer thickness absent
- Integration: 20-layer representative stack (negative + positive z) round-trips correctly

### 5.3 A2: validateStack — Tiered Validation

Validation is output-mode-aware and runs twice in the primary flow (before and after defaulting).

```
validate_identity():
    - No duplicate (layer_num, datatype) pairs                        [error]
    - name is non-empty for all entries                               [error]

validate_for_lyp():
    - All DisplayProps fields present and in range                    [error]
    - fill_alpha in [0, 255]                                          [error]

validate_for_3d():
    - All entries have PhysicalProps present                          [error]
    - thickness_nm >= 0.0 for all entries                             [error]
    - Overlapping z ranges with same material (non-parallel)          [warning]
    - Layers with z_start_nm straddling bonding plane (z_start < 0
      and z_start + thickness > 0)                                    [info]

validate_full():
    - All of the above
```

### 5.4 A7: readScript — 2.5D Ruby Script Parser

**Purpose:** Extract physical layer properties from a gks-generated 2.5D Ruby script.

**Scope:** Only gks-generated scripts with literal numeric values are supported. Scripts
containing Ruby variables, loops, or conditionals in the `z()` argument positions are
out of scope (NG2). If such constructs are detected, a WARNING is emitted and parsing
continues with a best-effort approach.

**What is parsed:**

The Klayout 2.5D script format is a flat or lightly grouped list of `z()` and `zz()` calls:

```ruby
z(input(1, 0), zstart: 0.1.um, height: 200.nm, name: "Metal1", fill: 0xff8800, frame: 0xaa4400)
z(input(2, 0), height: 300.nm)   # no zstart → follows previous layer

zz(name: "contacts", like: "4/0") do
  z(input(4, 0), zstart: 105.nm, height: 45.nm)
  z(input(5, 0), zstart: 105.nm, height: 45.nm)
end
```

**Parsing rules:**

1. Scan for `z(` and `zz(` call sites
2. For each `z()` call extract:
   - `input(layer_num, datatype)` → GDS identity key
   - `zstart` → `z_start_nm` (convert `.um` × 1000 or `.nm` × 1); absent = accumulate
   - `zstop` or `height` → `thickness_nm` (derive `thickness = zstop - zstart` if zstop used)
   - `color`, `fill`, `frame` → `DisplayProps` color fields (optional; override .lyp values if present)
   - `name` → layer name (optional; override if present)
   - `like` → noted but not used for import (it references the .lyp display style)
3. A `zz()` block indicates a parallel group; all `z()` calls within it are imported normally;
   the `zz()` name and display options are noted and may inform `layer_expression` in TOML
4. Unit suffix handling: `.um` multiplies by 1000 to get nm; `.nm` is used directly

**Merge behavior (UC4):** When script data is merged into a `LayerStack` already populated
from `.lyp`, matching is by `(layer_num, datatype)`. Unmatched keys in the script emit a
WARNING. Physical properties from the script are written into the matched `LayerEntry`;
existing `DisplayProps` from `.lyp` are preserved unless the script provides explicit
`fill`/`frame` values.

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
gks import [--lyp <file>] [--script <file>] -o <output.toml> [--verbose]

Options:
  --lyp <file>       Input Klayout .lyp layer properties file
  --script <file>    Input gks-generated 2.5D Ruby script
  -o <file>          Output TOML file (required)
  --verbose          Emit full diagnostic output

At least one of --lyp or --script must be provided.
If both are provided, properties are merged (see A7).
```

**Examples:**

```bash
# Migrate from .lyp only (physical props will be empty in output TOML)
gks import --lyp existing.lyp -o stack.toml

# Bootstrap from both artifacts — fully populated TOML
gks import --lyp existing.lyp --script existing.rb -o stack.toml
```

### 6.3 generate

```
gks generate <stack.toml> [--lyp <file>] [--script <file>] [--verbose]

Options:
  <stack.toml>       Input TOML stack definition (required)
  --lyp <file>       Output .lyp file
  --script <file>    Output 2.5D Ruby script
  --verbose          Emit full diagnostic output

At least one of --lyp or --script must be provided.
```

**Examples:**

```bash
# Generate both artifacts
gks generate stack.toml --lyp out.lyp --script out.rb

# Display-only: generate just the .lyp (no PhysicalProps required)
gks generate stack.toml --lyp out.lyp

# Regenerate after editing — the standard daily workflow
gks generate stack.toml --lyp new.lyp --script new.rb --verbose
```

### 6.4 validate

```
gks validate <stack.toml> [--for lyp|3d|full] [--verbose]

Options:
  <stack.toml>       Input TOML stack definition (required)
  --for <mode>       Validation tier: lyp, 3d, or full (default: full)
  --verbose          Include INFO-level diagnostics (bonding plane report, parallel groups)
```

### 6.5 Common Workflow: Full Round-Trip After Bootstrap

```bash
# One-time: import existing artifacts into TOML
gks import --lyp orig.lyp --script orig.rb -o stack.toml

# Review TOML, make edits...

# Regenerate — this is the daily workflow from here on
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
        ScriptReader.hpp     ← 2.5D Ruby script reader (A7)
        ScriptWriter.hpp     ← 2.5D Ruby script writer (A5)
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
      test_buildStack.cpp       ← z accumulation, negative z, parallel groups
      test_validator.cpp
      test_defaulter.cpp
      test_tomlReader.cpp
      test_lypReader.cpp
      test_scriptReader.cpp
    integration/
      test_roundtrip_toml_lyp.cpp
      test_roundtrip_toml_script.cpp
      test_bootstrap_import.cpp   ← .lyp + script → TOML → .lyp + script
      test_negative_z.cpp
  fixtures/
    asap7_stack.toml            ← reference stack for integration tests
    asap7.lyp
    asap7_25d.rb
  docs/
    PROJECT_SPEC.md
```

### 7.2 Modules & Boundaries

- **Module `core`**
  - Responsibilities: `LayerStack` types, `buildStack`, `Validator`, `Defaulter`
  - May depend on: standard library only
  - Must **not** depend on: CLI, io, XML library, TOML library, Ruby parsing

- **Module `io`**
  - Responsibilities: TOML parsing/writing, `.lyp` XML parsing/writing, 2.5D Ruby script
    parsing/writing
  - Depends on: `core`
  - Must **not** depend on: CLI

- **Module `cli`**
  - Responsibilities: subcommand dispatch, argument parsing, verbosity/logging control
  - Depends on: `core`, `io`

### 7.3 Naming Conventions

- Namespace: `gks` (all public types and functions)
- File names: `PascalCase.hpp` / `PascalCase.cpp` for classes; `camelCase.cpp` for free functions
- Error handling: `std::expected<T, GksError>` for recoverable errors; exceptions only at
  IO boundary for unrecoverable parse failures (malformed XML, unreadable file)

---

## 8. External Dependencies & Tooling

### 8.1 Languages & Versions

- **Primary language:** C++20
- **Build system:** CMake + Ninja
- **Supported platforms:** macOS 14+, Linux x86_64

### 8.2 Libraries

- **toml++** (`tomlplusplus`)
  - Purpose: TOML parsing and writing
  - Integration: FetchContent
  - Notes: Header-only; clean C++17/20 API; already used in PFXFlow

- **pugixml**
  - Purpose: `.lyp` XML parsing and writing
  - Integration: FetchContent or system package
  - Notes: Lightweight, fast, XPath available if needed

- **GoogleTest**
  - Purpose: Unit and integration testing
  - Integration: FetchContent

### 8.3 Testing & QA

- Unit tests: `tests/unit/` — GoogleTest, run via `ctest`
- Integration tests: `tests/integration/` — full file round-trips with fixtures
- Coverage target: 80%+ line coverage on `core/` and `io/`
- Reference fixture: ASAP7 (or sky130) representative layer stack

---

## 9. LLM Collaboration Plan

### 9.1 Role

- Draft data structures consistent with this spec
- Generate implementation from algorithm pseudocode
- Write test scaffolding
- Generate boilerplate (CMake, CLI argument parsing)

### 9.2 Working Protocol

- **Source of truth:** This document + actual repo
- **Update cycle:** Spec updated when design decisions change; relevant sections pasted into prompts
- **Patch format:** Complete file replacement preferred; unified diff acceptable for small changes;
  no stubs; code must compile

### 9.3 Prompt Cookbook

- "Given Section 4.3 structs and Section 5.2 pseudocode, implement `buildStack()` in `src/core/buildStack.cpp`."
- "Given Section 5.4 (A7), implement `ScriptReader` parsing `z()` and `zz()` calls with unit suffix handling."
- "Given Section 5.3 tiers, implement `Validator` in `src/core/Validator.cpp`."
- "Propose GoogleTests for `buildStack()` covering all edge cases in Section 5.2."
- "Implement `cmd_import.cpp` per the CLI design in Section 6.2."

---

## 10. Roadmap & Milestones

### 10.1 Phases

1. **Phase 0 – Skeleton**
   - Repo layout, CMake, blank modules, `ctest` smoke test, `fixtures/` directory

2. **Phase 1 – Core Data Model**
   - Implement all types in `LayerStack.hpp`
   - Implement `buildStack()` with signed z accumulation and diagnostic output
   - Unit tests for all edge cases in A1 (sequential, negative z, parallel groups, burial/gap warnings)

3. **Phase 2 – Validation & Defaulting**
   - Implement tiered `Validator` (A2)
   - Implement `Defaulter` with document-level defaults from `LayerStack::Defaults`
   - Unit tests

4. **Phase 3 – TOML I/O**
   - `TomlReader`: TOML → `vector<RawLayer>` → `buildStack()`
   - `TomlWriter`: `LayerStack` → TOML (all `z_start_nm` explicit in output)
   - Round-trip tests with reference fixture

5. **Phase 4 – .lyp I/O**
   - `LypReader`: `.lyp` XML → `LayerStack` (PhysicalProps absent)
   - `LypWriter`: `LayerStack` → `.lyp` XML
   - Round-trip tests for display columns

6. **Phase 5 – 2.5D Script I/O**
   - `ScriptWriter`: `LayerStack` → 2.5D Ruby script (A5)
   - `ScriptReader`: gks-generated script → `vector<RawLayer>` physical data (A7)
   - Round-trip tests; negative z fixture; parallel group `zz()` test

7. **Phase 6 – CLI**
   - `import`, `generate`, `validate` subcommands (Section 6)
   - End-to-end functional tests for all UCs
   - Bonding plane summary in verbose output

---

## 11. Open Questions, Risks & Parking Lot

### 11.1 Open Questions

- OQ1: Should `TomlWriter` emit only layers with `z_start_nm` explicitly provided, or always
  emit fully explicit absolute z for all layers? Current leaning: always emit explicit absolute z
  on output for unambiguity; the user edits the explicit form if they want to restructure.
- OQ2: Stipple and line style — store as Klayout integer index or as a descriptive string
  mapped to an index at emit time? String would be more portable in TOML.
- OQ3: How should the `like:` option in 2.5D scripts be handled on import? It references
  the `.lyp` display style by `"layer/datatype"` string. Currently: noted but ignored on
  import; `ScriptWriter` emits `like:` by default to keep scripts lean.
- OQ4: Should `layer_expression` / `zz()` support be in v1 or deferred to v2?
- OQ5: For technologies with deep negative z (e.g. backside power delivery + TSV), should the
  tool emit separate diagnostic sections for above-bond and below-bond layer counts and z extents?

### 11.2 Risks

- **Script parser fragility:** `ScriptReader` only handles gks-generated scripts. Hand-written
  scripts with variables will produce warnings and partial results. Mitigated by clear
  documentation of NG2.
- **Stipple/line_style index portability:** Indices may differ across Klayout versions.
  May need a mapping table or string-based approach (OQ2).
- **Negative z in Klayout 2.5D:** Verify that the Klayout `zstart:` option accepts negative
  values reliably across supported versions. Flag for early integration test.
- **Scope creep on `zz()` / boolean layer expressions:** Pin this to v2 unless trivial.

### 11.3 Parking Lot

- Color and stipple chooser GUI (FG1 — v2)
- YAML alternative input format (the `io` abstraction supports this at low cost)
- `layer_expression` / `zz()` parallel group rendering (reserved in IR; implement in v2)
- Per-substack TOML include/import for large PDKs with many layers

---

## 12. Appendices

### 12.1 Glossary

- **Canonical representation** — the single source of truth; in this tool, the TOML stack definition file
- **Bonding plane** — `z = 0`; the bonding interface in wafer-bonded 3D-IC technologies; layers above are positive z, layers below are negative z
- **Design layer** — a GDS `(layer_num, datatype)` pair representing designer intent
- **Process layer** — a physical structure realized during fabrication; may correspond to multiple design layers
- **Parallel layer group** — two or more design layers sharing the same `z_start_nm` because they are co-planar in the process stack (e.g., epi contact and gate contact)
- **High-water mark** — the maximum `z_start_nm + thickness_nm` seen so far during stack accumulation; the default `z_start_nm` for the next unspecified layer; signed (can be negative)
- **.lyp** — Klayout layer properties file; XML format defining layer display properties
- **2.5D script** — Klayout Ruby macro using `z()` / `zz()` calls to extrude GDS layers into a 3D scene
- **DisplayProps** — visual rendering properties (color, stipple, visibility)
- **PhysicalProps** — 3D physical geometry (signed z position, thickness, material)
- **RawLayer** — intermediate parsed representation of one TOML `[[layer]]` block, all fields optional

### 12.2 References

- Klayout 2.5D scripting API: https://www.klayout.de/doc-qt5/about/25d_view.html
- Klayout `.lyp` XML schema: inferred from Klayout source and existing `.lyp` files
- ASAP7 PDK: reference technology for integration testing fixtures
- toml++ library: https://github.com/marzer/tomlplusplus

### 12.3 Design Alternatives Considered

- **CSV as canonical format:** Rejected. No document-level metadata, no grouping, no inline
  comments, parallel groups expressed only implicitly. TOML provides all of these with a
  well-supported parser library and no custom grammar to maintain.
- **Custom DSL (LEF-like syntax):** Considered. Offers the best domain-specific readability
  and could make parallel groups first-class. Rejected for v1 because it requires building
  and maintaining a lexer/parser. Deferred to v2 if real-user adoption warrants it. The `io`
  module abstraction supports adding a new reader behind the same `LayerStack` IR interface.
- **Embedding 3D metadata in .lyp XML:** Rejected. Relies on undocumented Klayout parser
  tolerance; conflates rendering artifact with technology definition.
- **Integer z_order ranking instead of absolute z:** Rejected. A total order cannot represent
  parallel (co-planar) layers without arbitrary tie-breaking. Absolute signed z is the correct
  physical model.
- **Forcing all layers to specify absolute z_start_nm:** Rejected. Imposes unnecessary burden;
  most layers are sequential. Hybrid accumulation (Section 5.2) provides the right default
  with explicit override only where the geometry is genuinely non-trivial.
