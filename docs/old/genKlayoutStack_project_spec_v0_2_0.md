# Project: genKlayoutStack

## 0. Meta & Status

- **Owner:** Paul Gutwin
- **Doc status:** Draft
- **Last updated:** <2026-03-25>
- **Change log:**
  - <2026-03-20> – Initial draft
  - <2026-03-25> – Filled in architecture, data model, and z-ordering decisions from design review

---

## 1. Project Overview

### 1.1 Problem Statement

When working with Klayout, there are times when one needs to modify the enablement files,
specifically the layer definition file. Klayout itself is fine for making small adjustments,
but when one is working on a complex project and wishes to visualize everything in 3D,
keeping the layer map and 3D (2.5D in Klayout terms) expansion macro in sync, things get complicated.

What is needed is a tool that can create a consistent set of enablement (.lyp and 2.5D script) from a
CSV file and vice versa.

A very critical note: the 2.5D layer thicknesses and sequencing are metadata that are not (currently)
contained in the .lyp file. This implies that while the CSV and .lyp files are roughly peers in terms
of ground truth, generation of the 2.5D Ruby script requires additional details not included in the
layer map file.

**Decision:** The CSV file is the canonical representation of the technology stack. Both the `.lyp`
and the 2.5D Ruby script are derived output artifacts. This is discussed further in Section 3.

### 1.2 Goals

Concrete, testable goals.

- G1: Parse a CSV file and produce a Klayout layer file (.lyp - XML format) and a 2.5D Script (Ruby script)
- G2: Read a Klayout layer map file (.lyp) and generate a CSV file (migration path; 2.5D columns will be empty)
- G3: Handle poorly formed CSV files, e.g. blank cell information (e.g. missing color information) or completely missing columns (e.g. thickness)
- G4: Missing information should be assigned based on pre-defined standards or patterns, some of which can be controlled on the command line
- G5: Emit verbose, human-readable diagnostics describing all assumptions, defaults, and detected layer group structures (parallel z groups, accumulation resets, gaps)
- FG1: Future goal - For v2, include a color and stipple choosing tool (gui)

### 1.3 Non-Goals

- NG1: Command line only for v1. GUI only for v2.

### 1.4 Success Criteria

How do you know this project is "good enough" for v1?

- **Full round-trip:** `CSV → .lyp → CSV` is lossless for appearance (DisplayProps) columns
- **Migration path:** `.lyp → CSV → .lyp` is lossless for appearance columns; 2.5D (PhysicalProps) columns are left empty and must be filled in by the user — this is by design, not a deficiency
- Round-trip conversion with missing information is stable and converges on identical results
- All core functions have unit test coverage
- End-to-end functional tests for all major use cases

---

## 2. Users, Use Cases & Workflows

### 2.1 Target Users

- **Primary user(s):** Standard Cell Developers

### 2.2 Key Use Cases

- **UC1: Generate layer file from scratch**
  - Step 1: User creates a preliminary CSV definition of layers
  - Step 2: Tool responds with verbose description of issues with CSV — missing columns or fields, assumptions or defaults applied, detected parallel z groups, etc.
  - Output: Tool attempts to produce requested output

- **UC2: Modify layer file from input .lyp file**
  - Step 1: User provides tool with existing (functional) .lyp file; tool produces CSV output file with DisplayProps columns populated and PhysicalProps columns empty
  - Step 2: User adds thickness and z_start_nm data to CSV for 3D-capable output
  - Output: Tool generates new .lyp and/or 2.5D Ruby script conforming to CSV file

- **UC3: Generate 2.5D Script file**
  - Step 1: User provides existing CSV file as input (must have PhysicalProps columns populated)
  - Output: Tool generates 2.5D (Ruby) script

### 2.3 Example Scenarios

A couple of short, concrete stories that tie everything together.

*(To be filled in with real PDK layer examples, e.g. ASAP7 or sky130, once CSV column format is finalized.)*

---

## 3. Architecture Overview

### 3.1 Canonical Representation

**The CSV file is the single source of truth.** Both `.lyp` and the 2.5D Ruby script are generated
artifacts. The tool never treats a `.lyp` file as a primary input for 3D data — it is only a source
for migrating DisplayProps into a new CSV.

Data flow:

```
CSV  ──►  .lyp              (appearance artifact — DisplayProps only)
     ──►  2.5D Ruby script  (rendering artifact — PhysicalProps only)

.lyp ──►  CSV               (migration path — DisplayProps populated, PhysicalProps left empty)
```

The `.lyp → CSV` direction is a one-way migration tool for users with existing layer files. Once
in the CSV workflow, users never need to return through `.lyp`.

### 3.2 High-Level Component Diagram

```
┌─────────────────────────────────────────────────┐
│                     CLI                         │
│   (argument parsing, mode dispatch, verbosity)  │
└────────┬────────────────────────┬───────────────┘
         │                        │
         ▼                        ▼
┌────────────────┐      ┌────────────────────────┐
│   io/readers   │      │      io/writers         │
│                │      │                         │
│  CSV reader    │      │  CSV writer             │
│  .lyp reader   │      │  .lyp writer            │
│                │      │  2.5D Ruby writer        │
└────────┬───────┘      └────────────┬────────────┘
         │                           │
         ▼                           ▼
┌────────────────────────────────────────────────┐
│              core / LayerStack IR              │
│                                                │
│  LayerStack, LayerEntry, DisplayProps,         │
│  PhysicalProps, Validator, Defaulter           │
└────────────────────────────────────────────────┘
```

- `cli` → `io/readers` → `core` (build IR) → `core` (validate + default) → `io/writers`
- `core` exposes a stable C++ API consumed by CLI only

### 3.3 Processes & Data Flow

**CSV → .lyp + 2.5D script (primary flow):**

1. CSV reader parses rows into `std::vector<CSVRow>` (raw, unvalidated)
2. `buildStack()` transform:
   - Resolves z coordinates using hybrid accumulation (see Section 5)
   - Populates `LayerStack` IR
3. `Validator` runs tiered validation (identity, display, physical)
4. `Defaulter` fills missing DisplayProps using configured defaults; logs all substitutions
5. `Validator` runs again on completed IR
6. Writers emit requested output format(s)

**`.lyp` → CSV (migration flow):**

1. `.lyp` reader parses XML into `LayerStack` with PhysicalProps = `std::nullopt`
2. Validator runs identity + display validation only
3. CSV writer emits all columns; PhysicalProps columns are empty
4. Tool informs user that PhysicalProps must be filled in before 2.5D script can be generated

---

## 4. Data Model & Core Abstractions

### 4.1 Domain Concepts

- **LayerStack** — the complete technology layer definition; the top-level IR object
- **LayerEntry** — one row of the CSV; represents a single (GDS layer, datatype) pair
- **DisplayProps** — visual rendering properties; consumed by the `.lyp` writer
- **PhysicalProps** — 3D physical properties; consumed by the 2.5D Ruby writer
- **CSVRow** — raw parsed representation of one CSV row, prior to IR construction; may contain missing fields

**Key relationships:**
- A `LayerStack` owns an ordered `std::vector<LayerEntry>`
- Each `LayerEntry` has exactly one `DisplayProps` and zero or one `PhysicalProps`
- The `(layer_num, datatype)` pair is the primary key; duplicates are a validation error
- Multiple `LayerEntry` objects may share the same `z_start_nm` — this is a legal and common condition representing a **parallel layer group**

**Design layers vs. process layers:**
In real PDK stacks, multiple design layers (distinct GDS layer/datatype pairs) may physically
co-occupy the same z range because they are realized by the same process step (e.g., epi contact
and gate contact both formed by contact deposition). These are represented as separate `LayerEntry`
objects with identical `z_start_nm` values. The 2.5D Ruby writer may optionally render these as a
boolean union (`epi_contact OR gate_contact`) using a `layer_expression` override.

### 4.2 Core Data Structures

```cpp
// ─── Color ────────────────────────────────────────────────────────────────────

struct Color {
    uint8_t r, g, b;
};

// ─── DisplayProps ─────────────────────────────────────────────────────────────
//
// Responsibility: Visual appearance of a layer in the Klayout viewer.
//                 All fields are required for .lyp output.
//
// Fields:
//   fill_color    – interior fill color
//   frame_color   – border/outline color
//   fill_alpha    – opacity 0–255 (0 = transparent, 255 = opaque)
//   stipple_index – Klayout built-in stipple pattern index; -1 = solid fill
//   line_style    – Klayout line style index
//   visible       – layer visibility in viewer
//   valid         – layer enabled in viewer
//
// Invariants:
//   fill_alpha in [0, 255]
//   stipple_index >= -1
//
// Notes:
//   Stipple and line_style reference Klayout's internal pattern library by
//   index. The tool does not embed pattern definitions; Klayout resolves them.

struct DisplayProps {
    Color   fill_color;
    Color   frame_color;
    uint8_t fill_alpha    = 128;
    int     stipple_index = -1;   // -1 = solid fill
    int     line_style    = 0;
    bool    visible       = true;
    bool    valid         = true;
};

// ─── PhysicalProps ────────────────────────────────────────────────────────────
//
// Responsibility: 3D physical geometry of a layer in the process stack.
//                 Optional — absent for layers migrated from .lyp with no 3D data.
//
// Fields:
//   z_start_nm      – absolute bottom of layer in nm (always populated in IR;
//                     may be computed by accumulation from CSV input)
//   thickness_nm    – height in z; 0.0 permitted for marker/label layers
//   material        – process material class; informs 2.5D rendering color/style
//   layer_expression – optional boolean expression for multi-design-layer process
//                      layers, e.g. "epi_contact OR gate_contact"
//
// Invariants:
//   thickness_nm >= 0.0
//   z_start_nm >= 0.0
//
// Notes:
//   z_start_nm is always stored as absolute in the IR regardless of whether it
//   was explicitly provided or computed by accumulation during CSV parsing.

struct PhysicalProps {
    double      z_start_nm;
    double      thickness_nm;
    std::string material;                          // "metal", "dielectric", "poly", etc.
    std::optional<std::string> layer_expression;  // advanced: boolean layer combination
};

// ─── LayerEntry ───────────────────────────────────────────────────────────────
//
// Responsibility: Single entry in the layer stack; maps to one CSV row and
//                 one GDS (layer, datatype) pair.
//
// Fields:
//   layer_num    – GDS layer number
//   datatype     – GDS datatype (multiple datatypes per layer_num are valid)
//   name         – human-readable layer name, e.g. "Metal1"
//   purpose      – design intent, e.g. "drawing", "label", "pin"
//   display      – visual properties (always present)
//   physical     – 3D properties (optional; absent after .lyp migration)
//
// Invariants:
//   (layer_num, datatype) is unique within a LayerStack.
//   display is always populated (defaulted if missing in input).
//
// Notes:
//   It is common for one layer_num to have several datatype values.
//   Each (layer_num, datatype) pair must be a separate LayerEntry.
//   Example: Metal1 drawing (layer=31, dt=0), Metal1 label (layer=31, dt=10).

struct LayerEntry {
    int         layer_num;
    int         datatype;
    std::string name;
    std::string purpose;

    DisplayProps                display;
    std::optional<PhysicalProps> physical;
};

// ─── LayerStack ───────────────────────────────────────────────────────────────
//
// Responsibility: Top-level IR; the complete technology layer definition.
//                 Owns all LayerEntry objects.
//
// Fields:
//   tech_name – technology identifier, e.g. "ASAP7", "sky130"
//   version   – provenance/version string for the stack definition
//   layers    – ordered collection of layer entries
//
// Invariants:
//   No two entries share the same (layer_num, datatype) pair.
//   layers preserves CSV row order; sort-by-z occurs only at emit time in writers.
//
// Notes:
//   Created by buildStack() from a parsed CSV. Passed by const reference to
//   validators and writers.

struct LayerStack {
    std::string             tech_name;
    std::string             version;
    std::vector<LayerEntry> layers;
};
```

### 4.3 The CSVRow Intermediate Type

`CSVRow` is a raw parsed representation — all fields are `std::optional` — used between the CSV
reader and `buildStack()`. It is not part of the public API.

```cpp
struct CSVRow {
    std::optional<int>         layer_num;
    std::optional<int>         datatype;
    std::optional<std::string> name;
    std::optional<std::string> purpose;

    // DisplayProps (all optional at parse time; defaulted before IR construction)
    std::optional<Color>  fill_color;
    std::optional<Color>  frame_color;
    std::optional<uint8_t> fill_alpha;
    std::optional<int>    stipple_index;
    std::optional<int>    line_style;
    std::optional<bool>   visible;
    std::optional<bool>   valid;

    // PhysicalProps (all optional; physical block absent if none provided)
    std::optional<double>      z_start_nm;    // explicit override; absent = accumulate
    std::optional<double>      thickness_nm;
    std::optional<std::string> material;
    std::optional<std::string> layer_expression;

    int source_row;  // 1-based CSV row number, for diagnostics
};
```

### 4.4 CSV Column Format

The canonical CSV column order. All PhysicalProps columns may be omitted entirely for a
display-only workflow.

```
layer_num, datatype, name, purpose,
fill_color, frame_color, fill_alpha, stipple_index, line_style, visible, valid,
z_start_nm, thickness_nm, material, layer_expression
```

- `fill_color` and `frame_color` are hex RGB strings, e.g. `#FF8800`
- `z_start_nm` may be left blank; blank = compute by accumulation (see Section 5)
- `layer_expression` is optional and rarely used; see Section 4.1

### 4.5 Persistence & Serialization

- **Source of truth:** CSV file (human-editable, version-control friendly)
- **Output artifacts:** `.lyp` (XML, Klayout-defined schema), 2.5D Ruby script (Klayout scripting API)
- **Versioning:** `LayerStack.version` field carried through from CSV header comment or CLI flag; no internal binary format

---

## 5. Algorithms & Pipelines

### 5.1 Algorithm List

- A1: `buildStack` — CSV rows → LayerStack IR, including z-coordinate resolution
- A2: `validateStack` — tiered validation of a LayerStack IR
- A3: `applyDefaults` — fill missing DisplayProps using configured default rules
- A4: `writeLyp` — LayerStack IR → Klayout .lyp XML
- A5: `write2_5D` — LayerStack IR → Klayout 2.5D Ruby script
- A6: `readLyp` — Klayout .lyp XML → LayerStack IR (migration path; PhysicalProps absent)

### 5.2 A1: buildStack — Z Coordinate Resolution

**Purpose:** Convert raw CSVRows into a fully populated LayerStack IR, resolving z coordinates
using a hybrid accumulation model.

**The hybrid z model:**

Most process layers stack sequentially. Forcing users to specify absolute z coordinates for every
layer is error-prone and unnecessary. The tool uses the following rule:

> If `z_start_nm` is **blank** in the CSV row, the layer's z_start is set to the current
> **high-water mark** — the maximum `z_start_nm + thickness_nm` seen so far.
> If `z_start_nm` is **explicitly provided**, it is used directly, overriding accumulation.

This means:
- Sequential layers require only `thickness_nm`
- Parallel layers (same physical z range, different GDS layers) require `z_start_nm` to be
  specified on the second and subsequent members of the group
- The user is only forced to be explicit where the geometry is genuinely non-trivial

After processing any row (accumulated or explicit), the high-water mark is updated:

```
high_water = max(high_water, z_start_nm + thickness_nm)
```

**Inputs:** `std::vector<CSVRow>`, validated for required identity fields

**Outputs:** `LayerStack` with all `PhysicalProps.z_start_nm` populated as absolute values

**Pseudo-code:**

```text
function buildStack(rows: vector<CSVRow>) -> LayerStack:

    stack = LayerStack{}
    high_water = 0.0

    for row in rows:
        entry = LayerEntry{}
        entry.layer_num = row.layer_num
        entry.datatype  = row.datatype
        entry.name      = row.name
        entry.purpose   = row.purpose
        entry.display   = resolveDisplay(row)   // defaulting handled in A3

        if row has any PhysicalProps fields:
            if row.z_start_nm is present:
                z_start = row.z_start_nm        // explicit override
                if z_start < high_water - epsilon:
                    WARN "Layer '{name}' z_start={z_start} is below high_water={high_water}; possible unintentional layer burial"
                if z_start > high_water + epsilon:
                    WARN "Layer '{name}' z_start={z_start} leaves gap above high_water={high_water}"
            else:
                z_start = high_water            // accumulate

            thickness = row.thickness_nm ?? 0.0
            high_water = max(high_water, z_start + thickness)

            entry.physical = PhysicalProps{
                z_start_nm   = z_start,
                thickness_nm = thickness,
                material     = row.material ?? "",
                layer_expression = row.layer_expression
            }
        else:
            entry.physical = nullopt

        stack.layers.push_back(entry)

    return stack
```

**Diagnostic output (G5):** For each layer, emit one INFO line describing whether z was accumulated
or explicit. Detect and name parallel groups:

```
INFO  'substrate'    : z=[0.0, 300.0] nm  (accumulated)
INFO  'diffusion'    : z=[300.0, 350.0] nm  (accumulated)
INFO  'epi_contact'  : z=[105.0, 150.0] nm  (explicit — parallel group start)
INFO  'gate_contact' : z=[105.0, 150.0] nm  (explicit — parallel group {epi_contact, gate_contact})
INFO  'm0'           : z=[150.0, 186.0] nm  (accumulated from high_water=150.0)
```

**Edge cases:**
- `z_start_nm` explicit but below high_water → WARN (burial)
- `z_start_nm` explicit but above high_water → WARN (gap)
- `thickness_nm` absent or 0.0 → permitted; layer is a 2D marker
- All PhysicalProps columns absent → `physical = nullopt`; display-only layer

**Testing strategy:**
- Unit: sequential-only stack, accumulation is correct
- Unit: parallel group — two rows with same explicit z_start, high_water advances past both
- Unit: gap warning triggered correctly
- Unit: burial warning triggered correctly
- Integration: full 20-layer representative stack round-trips correctly

### 5.3 A2: validateStack — Tiered Validation

Validation is output-mode-aware. Running with `--output lyp` need not require PhysicalProps.

```
validate_identity()   → no duplicate (layer_num, datatype) pairs; name non-empty
validate_for_lyp()    → all DisplayProps fields present and in range
validate_for_3d()     → all PhysicalProps present; thickness_nm >= 0; z_start_nm >= 0;
                        warn on overlapping z ranges with same material (probably unintentional)
validate_full()       → all of the above
```

Validation runs **twice** in the primary flow: once before defaulting (to report raw issues) and
once after (to confirm the IR is complete before emit).

---

## 6. Codebase Layout & Module Boundaries

### 6.1 Directory Structure

```text
genKlayoutStack/
  CMakeLists.txt
  include/
    genKlayoutStack/
      LayerStack.hpp       ← core types (LayerEntry, DisplayProps, PhysicalProps, LayerStack)
      Validator.hpp
      Defaulter.hpp
      io/
        CsvReader.hpp
        CsvWriter.hpp
        LypReader.hpp
        LypWriter.hpp
        ScriptWriter.hpp   ← 2.5D Ruby writer
  src/
    core/
      LayerStack.cpp
      Validator.cpp
      Defaulter.cpp
      buildStack.cpp
    io/
      CsvReader.cpp
      CsvWriter.cpp
      LypReader.cpp
      LypWriter.cpp
      ScriptWriter.cpp
    cli/
      main.cpp
  tests/
    unit/
      test_buildStack.cpp
      test_validator.cpp
      test_defaulter.cpp
      test_csvReader.cpp
      test_lypReader.cpp
    integration/
      test_roundtrip_csv_lyp.cpp
      test_roundtrip_migration.cpp
      test_full_stack.cpp
  docs/
    PROJECT_SPEC.md
```

### 6.2 Modules & Boundaries

- **Module `core`**
  - Responsibilities: `LayerStack` types, `buildStack`, `Validator`, `Defaulter`
  - May depend on: standard library only
  - Must **not** depend on: CLI, io, any XML or Ruby library

- **Module `io`**
  - Responsibilities: CSV parsing/writing, .lyp XML parsing/writing, 2.5D Ruby script writing
  - Depends on: `core`
  - Must **not** depend on: CLI

- **Module `cli`**
  - Responsibilities: argument parsing, mode dispatch, verbosity/logging control
  - Depends on: `core`, `io`

### 6.3 Naming Conventions

- Namespaces: `gks::core`, `gks::io`, `gks::cli`
- File names: `PascalCase.hpp` / `PascalCase.cpp` for classes; `camelCase.cpp` for free functions
- Error handling: `std::expected<T, GksError>` preferred; exceptions only at IO boundary for
  unrecoverable parse failures

---

## 7. External Dependencies & Tooling

### 7.1 Languages & Versions

- **Primary language:** C++20
- **Build system:** CMake + Ninja
- **Supported platforms:** macOS 14+, Linux x86_64

### 7.2 Libraries

- **pugixml** (or tinyxml2)
  - Purpose: .lyp XML parsing and writing
  - Integration: FetchContent or system package TBD
- **fast-cpp-csv-parser** or hand-rolled CSV reader
  - Purpose: CSV parsing; hand-rolled preferred given limited column complexity
  - Integration: single-header vendored under `external/`
- **GoogleTest**
  - Purpose: unit and integration testing
  - Integration: FetchContent

### 7.3 Testing & QA

- Unit tests: `tests/unit/` — GoogleTest, run via `ctest`
- Integration tests: `tests/integration/` — full file round-trips with known-good fixtures
- Coverage target: 80%+ line coverage on `core/` and `io/`

---

## 8. LLM Collaboration Plan

### 8.1 Role

- Draft data structures consistent with this spec
- Generate boilerplate code / glue
- Propose algorithms and refine them
- Write test scaffolding

### 8.2 Working Protocol

- **Source of truth:** This document + actual repo
- **Update cycle:** Spec updated when design decisions change; relevant spec sections pasted into prompts
- **Patch format:** Unified diff or complete file replacement; no stubs; code must compile

### 8.3 Prompt Cookbook

- "Given Section 4.2 and this header, generate the implementation of `buildStack()`."
- "Based on A2 in Section 5.3, implement `validateStack()` with these four tiers."
- "Propose GoogleTests for `buildStack()` covering the edge cases listed in Section 5.2."

---

## 9. Roadmap & Milestones

### 9.1 Phases

1. **Phase 0 – Skeleton**
   - Repo layout, CMake, blank modules, CI smoke test

2. **Phase 1 – Core Data Model**
   - Implement `LayerStack`, `LayerEntry`, `DisplayProps`, `PhysicalProps` types
   - Implement `buildStack()` with z accumulation logic
   - Unit tests for all edge cases in A1

3. **Phase 2 – Validation & Defaulting**
   - Implement tiered `Validator`
   - Implement `Defaulter` with configurable default rules
   - Unit tests

4. **Phase 3 – I/O: CSV**
   - CSV reader → CSVRow → buildStack pipeline
   - CSV writer (emits fully explicit z_start_nm for all rows)
   - Round-trip tests

5. **Phase 4 – I/O: .lyp**
   - .lyp XML reader (migration path)
   - .lyp XML writer
   - Round-trip tests for display columns

6. **Phase 5 – I/O: 2.5D Ruby Script**
   - 2.5D Ruby script writer
   - Integration test with representative PDK layer stack

7. **Phase 6 – CLI & UX**
   - Full CLI wrapping all modes
   - Verbose diagnostic output (G5)
   - End-to-end functional tests

---

## 10. Open Questions, Risks & Parking Lot

### 10.1 Open Questions

- OQ1: Should the CSV writer emit absolute `z_start_nm` for all rows (maximally explicit) or
  attempt to reproduce the sparse/hybrid input (more human-friendly)? Current leaning: always
  emit absolute for unambiguity; users edit the explicit form.
- OQ2: Stipple and line style — store as Klayout integer index, or as a descriptive string that
  the tool maps to an index? String would be more portable and readable in CSV.
- OQ3: Exact XML library choice — pugixml vs tinyxml2. Pugixml has better XPath support if needed.
- OQ4: Should `layer_expression` (boolean union for parallel process layers) be in v1 or deferred to v2?

### 10.2 Risks

- **XML schema fragility:** embedding non-standard data in .lyp is explicitly out of scope (Section 3.1). Risk is low as long as this decision holds.
- **Stipple/line_style index portability:** indices may differ across Klayout versions. May need a mapping table.
- **Scope creep on 2.5D script complexity:** Klayout's 2.5D scripting API has many options. Pin the feature set early.

### 10.3 Parking Lot

- Color and stipple chooser GUI (FG1 — v2)
- YAML or TOML alternative to CSV as canonical format (the `io` module abstraction supports this at low cost)
- Boolean layer expression support in 2.5D writer (`layer_expression` field is reserved in the IR)

---

## 11. Appendices

### 11.1 Glossary

- **Canonical representation** — the single source of truth; in this tool, the CSV file
- **Design layer** — a GDS (layer_num, datatype) pair representing designer intent
- **Process layer** — a physical film/structure realized during fabrication; may correspond to multiple design layers
- **Parallel layer group** — two or more design layers sharing the same z_start_nm and thickness_nm because they are co-planar in the process stack (e.g., epi contact and gate contact)
- **High-water mark** — the maximum `z_start_nm + thickness_nm` seen so far during stack accumulation; the default z_start for the next unspecified layer
- **.lyp** — Klayout layer properties file; XML format defining layer display properties
- **2.5D script** — Klayout Ruby macro that extrudes GDS layers into a 3D scene using z coordinates and thickness values
- **DisplayProps** — visual rendering properties (color, stipple, visibility)
- **PhysicalProps** — 3D physical geometry properties (z position, thickness, material)

### 11.2 References

- Klayout .lyp XML schema: inferred from Klayout source / existing .lyp files
- Klayout 2.5D scripting API: https://www.klayout.de/doc/programming/2_5d_view.html
- ASAP7 PDK: reference technology for integration testing

### 11.3 Design Alternatives Considered

- **Embedding 3D metadata in .lyp XML comments:** Rejected. Relies on undocumented parser tolerance; conflates rendering artifact with technology definition; complicates the round-trip model.
- **Using integer z_order (rank) instead of absolute z coordinates:** Rejected. Partial order (parallel layers) cannot be represented in a total order without arbitrary tie-breaking. Absolute z is the correct physical model.
- **Forcing all layers to specify absolute z_start_nm:** Rejected. Imposes unnecessary burden on users; most layers in a real stack are sequential. Hybrid accumulation (Section 5.2) provides the right default with explicit override for exceptional cases.
