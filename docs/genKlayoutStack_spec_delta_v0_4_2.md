# genKlayoutStack spec delta: v0.4.1 → v0.4.2

Apply these section replacements to `docs/PROJECT_SPEC.md`.
All other sections are unchanged.

## Change log entry (add to Section 0)

```
- <2026-03-27> – Two-pass z accumulation with explicit z=0 anchor;
                  align_bottom_to / align_top_to alignment keywords;
                  topological sort for alignment dependency resolution
```

---

## Section 4.3 — RawLayer struct (replace existing)

Add two new optional fields to `RawLayer`:

```cpp
struct RawLayer {
    std::optional<int>         layer_num;
    std::optional<int>         datatype;
    std::optional<std::string> name;
    std::optional<std::string> purpose;

    // DisplayProps
    std::optional<Color>   fill_color;
    std::optional<Color>   frame_color;
    std::optional<uint8_t> fill_alpha;
    std::optional<int>     dither_pattern;
    std::optional<int>     line_style;
    std::optional<bool>    visible;
    std::optional<bool>    valid;
    std::optional<bool>    transparent;

    // PhysicalProps
    std::optional<double>      z_start_nm;       // absent → accumulate;
                                                  // 0.0 → z anchor
    std::optional<double>      thickness_nm;
    std::optional<std::string> material;
    std::optional<std::string> layer_expression;

    // Alignment (new)
    // Format: "layer_name:edge" where edge is "top" or "bottom"
    // Example: "epi_contact:bottom"
    // align_bottom_to: snaps the BOTTOM of this layer to the named edge
    // align_top_to:    snaps the TOP    of this layer to the named edge
    // Both may be present simultaneously; see Section 5.2a for resolution rules.
    // Must not be combined with explicit z_start_nm (ERROR).
    std::optional<std::string> align_bottom_to;
    std::optional<std::string> align_top_to;

    int source_line = 0;
};
```

---

## Section 4.5 — TOML format (add to examples)

Add the following to the TOML format section, after the existing parallel
group example:

### Z anchor

Place `z_start_nm = 0.0` on the layer that defines the physical reference
plane (e.g. the bonding plane, or the top of the substrate). Layers above
it in the file accumulate upward; layers below it accumulate downward.

```toml
[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
fill_color   = "#006600"
frame_color  = "#004400"
# z_start_nm omitted → accumulated upward from anchor

[[layer]]
name         = "poly"
layer_num    = 3
datatype     = 0
fill_color   = "#FFAA00"
frame_color  = "#CC8800"
z_start_nm   = 0.0          # ← z anchor: this layer's bottom = z=0
thickness_nm = 22.0
material     = "poly"

[[layer]]
name         = "substrate"
layer_num    = 0
datatype     = 0
fill_color   = "#888888"
frame_color  = "#555555"
# z_start_nm omitted → accumulated downward from anchor
thickness_nm = 300.0
material     = "silicon"
# resolved z_start_nm will be -300.0
```

### Alignment keywords

```toml
# Standard parallel group — both contacts share poly's top edge as their bottom
[[layer]]
name             = "epi_contact"
layer_num        = 4
datatype         = 0
fill_color       = "#FF8800"
frame_color      = "#CC6600"
align_bottom_to  = "poly:top"      # my bottom snaps to poly's top
thickness_nm     = 45.0
material         = "tungsten"

[[layer]]
name             = "gate_contact"
layer_num        = 5
datatype         = 0
fill_color       = "#8800FF"
frame_color      = "#6600CC"
align_bottom_to  = "poly:top"      # same — parallel group, explicit intent
thickness_nm     = 30.0            # different thickness than epi_contact
material         = "tungsten"

# Fully constrained layer — both edges locked; thickness ignored with warning
[[layer]]
name             = "gate_oxide"
layer_num        = 2
datatype         = 0
fill_color       = "#FFFF00"
frame_color      = "#CCCC00"
align_bottom_to  = "substrate:top"
align_top_to     = "poly:bottom"
thickness_nm     = 5.0             # WARN if this doesn't match derived height
material         = "oxide"
```

---

## Section 5.2 — buildStack: two-pass accumulation (full replacement)

**Purpose:** Resolve `z_start_nm` for all layers using a two-pass model
with an explicit z anchor and alignment dependency resolution.

### Overview

Processing happens in three stages:

1. **Alignment resolution** — topological sort + resolve all `align_bottom_to`
   / `align_top_to` references (Section 5.2a)
2. **Pass 1: upward accumulation** — layers above the anchor in the file,
   processed bottom-up (anchor first, toward top of file)
3. **Pass 2: downward accumulation** — layers below the anchor in the file,
   processed top-down (just below anchor first, toward bottom of file)

### Finding the anchor

Scan the layer list for the first layer with `z_start_nm == 0.0` explicitly
set. This is the anchor layer. Its `z_start_nm` is 0.0 by definition.

If no anchor is found: `anchor_idx = last layer in list`. The entire stack
accumulates upward from 0.0, which preserves backward compatibility with
existing TOML files that don't use the anchor feature.

### Pass 1 — upward accumulation (layers above anchor)

Layers above the anchor in the file have higher physical z (top of file =
top of stack). Process them in reverse file order starting from the anchor
and moving toward the top of the file.

```
hw_pos = anchor.z_start_nm + anchor.thickness_nm   // = thickness of anchor

for i = anchor_idx - 1 downto 0:
    layer = raw[i]

    if layer has alignment: z_start = resolved (see 5.2a)
    elif layer.z_start_nm present and > 0: z_start = layer.z_start_nm
    else: z_start = hw_pos

    hw_pos = max(hw_pos, z_start + thickness)
    layer.resolved_z_start = z_start
```

### Pass 2 — downward accumulation (layers below anchor)

Layers below the anchor in the file have lower physical z (bottom of file =
bottom of stack). Process them in file order starting just below the anchor.

```
hw_neg = anchor.z_start_nm   // = 0.0

for i = anchor_idx + 1 to end:
    layer = raw[i]

    if layer has alignment: z_start = resolved (see 5.2a)
    elif layer.z_start_nm present and < 0: z_start = layer.z_start_nm
    else: z_start = hw_neg - thickness

    hw_neg = min(hw_neg, z_start)
    layer.resolved_z_start = z_start
```

### Diagnostic output

```
INFO  'substrate'   : z=[-300.0,   0.0] nm  (accumulated downward)
INFO  'poly'        : z=[   0.0,  22.0] nm  (anchor — z=0 reference plane)
INFO  'epi_contact' : z=[  22.0,  67.0] nm  (align_bottom_to poly:top)
INFO  'gate_contact': z=[  22.0,  52.0] nm  (align_bottom_to poly:top — parallel group)
INFO  'm0'          : z=[  67.0, 103.0] nm  (accumulated upward from hw=67.0)

SUMMARY  Anchor layer: 'poly' at z=0.0
SUMMARY  Parallel groups: 1 detected — {epi_contact, gate_contact}
SUMMARY  Layers below bonding plane: 1  (substrate)
SUMMARY  Layers above bonding plane: 4
```

### Backward compatibility

If no layer has `z_start_nm = 0.0`, the anchor defaults to the last layer
and the entire stack accumulates upward from 0.0. Existing TOML files
produce identical output to v0.4.1.

---

## Section 5.2a — Alignment resolution (new section)

**Purpose:** Resolve `align_bottom_to` and `align_top_to` references before
accumulation runs. This is a prerequisite pass that populates
`resolved_z_start` on all aligned layers.

### String format

```
"layer_name:edge"

layer_name  — the value of the `name` field of the target layer
edge        — "top" or "bottom"
```

Examples:
```
"poly:top"        → the top edge of poly    = poly.z_start + poly.thickness
"poly:bottom"     → the bottom edge of poly = poly.z_start
"substrate:top"   → the top edge of substrate
```

### Resolution rules

| Fields present | z_start derivation | thickness derivation |
|---|---|---|
| `align_bottom_to` only | `ref.edge` | from `thickness_nm` or default |
| `align_top_to` only | `ref.edge - thickness` | from `thickness_nm` or default |
| Both | `bottom_ref.edge` | `top_ref.edge - bottom_ref.edge` |
| Both + `thickness_nm` present | `bottom_ref.edge` | derived from alignment; WARN if `thickness_nm` differs from derived height by more than epsilon |

### Conflict rules

| Condition | Severity | Message |
|---|---|---|
| `align_*` + explicit `z_start_nm` both present | ERROR | "align_* and z_start_nm are mutually exclusive on layer '{name}'" |
| Reference layer name not found | ERROR | "align target '{name}' not found in stack" |
| Circular reference (A→B→A) | ERROR | "circular alignment reference: {A} → {B} → {A}" |
| Both alignments present + thickness present + mismatch > epsilon | WARN | "thickness_nm={n} ignored; derived height from alignment is {m}nm" |
| `align_top_to` only + no thickness + no default | ERROR | "align_top_to requires thickness_nm or a document default to derive z_start" |

### Topological sort

Because alignment references can point to layers either above or below in
the file, resolution order is not guaranteed by document order. A
topological sort is required:

```text
function resolveAlignments(layers: vector<RawLayer>) -> void:

    // Build dependency graph
    // Edge: A → B means A depends on B being resolved first
    graph = {}
    for each layer with align_bottom_to or align_top_to:
        extract ref_name from alignment string
        add edge: this_layer → ref_name

    // Detect cycles (DFS)
    if cycle detected: emit ERROR for each cycle, abort

    // Process in topological order
    // Layers without alignment are pre-resolved (z_start from z_start_nm
    // or deferred to accumulation pass)
    for layer in topological_order:
        if layer has alignment:
            ref = find layer by name
            if ref not yet resolved: ERROR (should not happen after topo sort)
            compute z_start from alignment formula
            mark layer as alignment-resolved
```

### Interaction with accumulation passes

Alignment-resolved layers have a fixed `z_start`. They still update
`hw_pos` or `hw_neg` after placement so subsequent accumulated layers
stack correctly relative to them.

---

## Section 5.3 — validateStack: new rules (add to existing tiers)

Add to `validate_identity()`:
```
- align_bottom_to or align_top_to references a name not in the stack  [error]
- align_* and z_start_nm both present on same layer                   [error]
- circular alignment reference                                         [error]
```

Add to `validate_for_3d()`:
```
- align_top_to only + no thickness + no default                        [error]
- both alignments + thickness present + mismatch > 0.001nm            [warning]
- anchor layer (z_start_nm = 0.0) absent from stack                   [info]
  (not an error — backward compat; single-pass mode used)
```

---

## Section 10.1 — Close/update open questions

**OQ1 (TomlWriter explicit z output):** Unchanged — always emit explicit
`z_start_nm` on output. Alignment fields (`align_bottom_to`,
`align_top_to`) are NOT emitted by TomlWriter — the output TOML always
uses resolved absolute z coordinates. This ensures generated TOML is
order-independent and unambiguous.

**New OQ6:** Should the TomlWriter have a `--preserve-alignment` flag that
round-trips the alignment keywords rather than replacing them with resolved
z coordinates? Deferred to v2 — v1 always emits explicit z.

---

## Phase 1 task additions (Section 10 / Roadmap)

Add to Phase 1 tasks in CLAUDE.md:

```
- src/core/buildStack.cpp: implement two-pass accumulation per spec 5.2
  - find anchor layer (z_start_nm == 0.0)
  - pass 1: upward from anchor toward top of file
  - pass 2: downward from anchor toward bottom of file
  - backward compat: no anchor → single upward pass from last layer

- src/core/buildStack.cpp: implement alignment resolution per spec 5.2a
  - parse "layer_name:edge" string format
  - build dependency graph
  - DFS cycle detection
  - topological sort
  - resolve z_start for all aligned layers before accumulation passes

Additional unit tests for test_buildStack.cpp:
  - anchor in middle of stack: layers above positive z, below negative z
  - no anchor: backward compat, full upward accumulation
  - align_bottom_to: parallel group with explicit alignment
  - align_top_to: layer snapped to top of reference
  - both alignments: thickness derived, warning on mismatch
  - align + z_start_nm: ERROR
  - circular reference: ERROR
  - unknown reference name: ERROR
  - alignment reference pointing above anchor (upward pass)
  - alignment reference pointing below anchor (downward pass)
```
