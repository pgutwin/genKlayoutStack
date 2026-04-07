---
title: "gks TOML Z-Stack Cheat Sheet"
subtitle: "genKlayoutStack v0.4.2"
pdf-engine: xelatex
monofont: "Menlo"
geometry: margin=0.6in
numbersections: true
toc-depth: 3
---
# gks TOML Z-Stack Cheat Sheet

How to define layer positions in a `genKlayoutStack` TOML file.

---

## The mental model: a stack of pancakes

Imagine laying pancakes on a plate, one at a time. Each pancake sits on top of the
previous one. You only need to know how *thick* each pancake is -- the position takes
care of itself. That is the default behavior in gks.

The exception is when you need to place a pancake at a *specific* height regardless
of what came before, or when two pancakes occupy the *same* height at the same time
(parallel layers). Those cases require an explicit `z_start_nm` or an alignment keyword.

---

## TOML ordering convention

**The first `[[layer]]` in the file is the top of the physical stack. The last
`[[layer]]` is the bottom.**

This matches the way you would draw a process cross-section on a whiteboard --
top of the page is top of the stack. Internally, `buildStack()` reverses document
order before running accumulation, so z increases from the last layer in the file
upward to the first. The IR stores layers in physical bottom-to-top order
(lowest z first); this is an internal detail and does not affect how you write TOML.

---

## The three basic cases

![z semantics](z_semantics.svg)

### Case 1 -- accumulated (the common case)

Omit `z_start_nm`. The layer stacks on top of whatever came before it in the
physical stack -- i.e., whatever comes after it in the file.

```toml
[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
# z_start_nm NOT present -> computed automatically
thickness_nm = 36.0
material     = "metal"
```

**Rule:** `z_start = high_water` at the time this layer is processed.
`high_water` then advances to `z_start + thickness_nm`.

---

### Case 2 -- explicit override

Provide `z_start_nm` to place a layer at a precise position, regardless of
what came before. The tool will warn if this creates a gap or a burial
relative to the current `high_water`.

```toml
[[layer]]
name         = "epi_contact"
layer_num    = 4
datatype     = 0
z_start_nm   = 105.0    # explicit: placed here regardless of high_water
thickness_nm = 45.0
material     = "tungsten"
```

**Rule:** `z_start = z_start_nm` (literal). `high_water` advances to
`max(high_water, z_start + thickness_nm)`.

**Gap warning:** if `z_start_nm > high_water`, gks warns that there is
unaccounted space between the previous layer top and this one.

**Burial warning:** if `z_start_nm < high_water`, gks warns that this layer
starts below the top of a previous layer.

---

### Case 3 -- parallel group via explicit z

Two or more layers that physically co-exist at the same z range (e.g. epi
contact and gate contact formed in the same deposition step). Both get the
same explicit `z_start_nm`. `high_water` advances only once, to
`max(all z_top values in the group)`.

```toml
[[layer]]
name         = "epi_contact"
layer_num    = 4
datatype     = 0
z_start_nm   = 105.0    # explicit: parallel group start
thickness_nm = 45.0
material     = "tungsten"

[[layer]]
name         = "gate_contact"
layer_num    = 5
datatype     = 0
z_start_nm   = 105.0    # explicit: same z -> parallel group
thickness_nm = 45.0
material     = "tungsten"
```

See also: the preferred approach using `align_bottom_to` in the Alignment
section below, which makes the parallel relationship explicit by name rather
than by coincidental z value.

---

## The z = 0 anchor

By default the last layer in the file starts at z = 0 and the stack
accumulates upward. This works fine for simple front-side-only stacks.

For technologies with backside structures (backside power delivery, TSVs,
bonded wafers), you need layers both above and below z = 0. Do this by
marking one layer as the **anchor** with `z_start_nm = 0.0`:

```toml
[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
thickness_nm = 36.0
material     = "metal"
# z_start_nm omitted -> accumulates upward from anchor

[[layer]]
name         = "poly"
layer_num    = 3
datatype     = 0
z_start_nm   = 0.0      # ANCHOR: poly's bottom = bonding plane (z=0)
thickness_nm = 22.0
material     = "poly"

[[layer]]
name         = "substrate"
layer_num    = 0
datatype     = 0
thickness_nm = 300.0
material     = "silicon"
# z_start_nm omitted -> accumulates downward from anchor
# resolves to z_start = -300.0
```

**Rules:**

- The anchor is the first layer in the file with `z_start_nm = 0.0` explicitly set.
- Layers above the anchor in the file accumulate upward from the anchor's top edge.
- Layers below the anchor in the file accumulate downward from z = 0, producing
  negative z values automatically.
- If no anchor is present, the last layer in the file is the implicit anchor at z = 0
  and the whole stack accumulates upward -- identical to pre-v0.4.2 behavior.

**Diagnostic output with anchor:**

```
INFO  'substrate'  : z=[-300.0, 0.0] nm   (accumulated downward from hw=0.0)
INFO  'poly'       : z=[0.0, 22.0] nm     (anchor -- z=0 reference plane)
INFO  'm0'         : z=[22.0, 58.0] nm    (accumulated upward from hw=22.0)
SUMMARY  Anchor layer: 'poly' at z=0.0
```

---

## Alignment keywords

Alignment keywords declare an explicit geometric relationship between layers by
name. This is cleaner than matching `z_start_nm` values for parallel groups, and
essential when a layer's position is defined by its relationship to another layer
rather than by an absolute coordinate.

### align_bottom_to

Snaps the **bottom edge** of this layer to the named edge of a reference layer.

```toml
[[layer]]
name            = "epi_contact"
layer_num       = 4
datatype        = 0
align_bottom_to = "poly:top"    # my bottom = poly's top edge
thickness_nm    = 45.0
material        = "tungsten"
```

Format: `"layer_name:top"` or `"layer_name:bottom"`

- `"poly:top"` -- the top edge of poly = `poly.z_start + poly.thickness`
- `"poly:bottom"` -- the bottom edge of poly = `poly.z_start`

### align_top_to

Snaps the **top edge** of this layer to the named edge of a reference layer.
`z_start` is derived as `ref_edge - thickness_nm`.

```toml
[[layer]]
name         = "gate_oxide"
layer_num    = 2
datatype     = 0
align_top_to = "poly:bottom"    # my top = poly's bottom edge
thickness_nm = 5.0
material     = "oxide"
# if poly.z_start = 0, then gate_oxide.z_start = 0 - 5 = -5
```

### Both keywords together -- fully constrained layer

When both `align_bottom_to` and `align_top_to` are specified, the layer's
position and thickness are both derived from the alignment. Any `thickness_nm`
present in the TOML is ignored (with a warning) if it does not match the derived height.

```toml
[[layer]]
name             = "gate_oxide"
layer_num        = 2
datatype         = 0
align_bottom_to  = "substrate:top"   # my bottom = top of substrate
align_top_to     = "poly:bottom"     # my top    = bottom of poly
# thickness_nm not needed; derived = poly.bottom - substrate.top
# if thickness_nm is present and doesn't match derived value: WARN
material         = "oxide"
```

### Parallel group via alignment (preferred)

Using `align_bottom_to` for a parallel group is cleaner than matching
`z_start_nm` values because it names the relationship explicitly and handles
unequal thicknesses without false burial warnings:

```toml
[[layer]]
name            = "epi_contact"
layer_num       = 4
datatype        = 0
align_bottom_to = "poly:top"     # my bottom snaps to poly's top
thickness_nm    = 45.0
material        = "tungsten"

[[layer]]
name            = "gate_contact"
layer_num       = 5
datatype        = 0
align_bottom_to = "poly:top"     # same reference -> parallel group
thickness_nm    = 30.0           # different thickness is fine
material        = "tungsten"
```

gks detects this as a parallel group and reports it in diagnostics:

```
INFO  'epi_contact'  : z=[22.0, 67.0] nm  (alignment-resolved -- parallel group {...})
INFO  'gate_contact' : z=[22.0, 52.0] nm  (alignment-resolved -- parallel group {...})
```

`high_water` advances to `max(22+45, 22+30) = 67` after the group.

---

## Alignment conflict rules

| Condition | Result |
|-----------|--------|
| `align_*` and `z_start_nm` both present on same layer | ERROR |
| Reference layer name not found in stack | ERROR |
| Circular reference (A->B->A) | ERROR |
| Both alignments + `thickness_nm` present + values differ | WARN; alignment wins |
| `align_top_to` only, no `thickness_nm`, no default | ERROR |

Alignment references can point to any other layer in the stack regardless of
position in the file. The reference layer must have a resolved z position --
meaning it has an explicit `z_start_nm`, is itself aligned to something explicit,
or is the anchor layer.

---

## The placeholder pattern

Claude Code inserts these commented-out stubs as reminders. The `???` values
are intentionally unparseable so you cannot accidentally leave them in place:

```toml
# z_start_nm   = ???    # optional: explicit z start (nm, signed); omit to accumulate
# thickness_nm = ???    # required for 3D output
# material     = ???    # required for 3D output
```

For a typical sequential layer you only need to fill in two fields:

```toml
thickness_nm = 36.0
material     = "metal"
```

For a parallel group using alignment:

```toml
align_bottom_to = "ref_layer:top"
thickness_nm    = 45.0
material        = "tungsten"
```

---

## The coordinate system

`z = 0` is the **bonding plane**. Negative z is below (substrate, backside
metal, TSVs). Positive z is above (FEOL, BEOL metals).

```
        +z  ^
             |   m1, v0, m0, contacts, poly
-------------|-------------  z = 0  (bonding plane)
             |   substrate, backside metal, TSVs
        -z  v
```

Layers below the bonding plane appear **last** in the TOML file and either
use a negative `z_start_nm` or omit it (accumulating downward automatically
when an anchor is present).

---

## Document-level defaults

Rather than repeating the same `thickness_nm` or `material` on every layer,
set a default in `[stack.defaults]`. Per-layer values override it.

```toml
[stack.defaults]
thickness_nm = 36.0       # used for any layer that omits thickness_nm
material     = "metal"    # used for any layer that omits material
```

A layer that only differs from the default in color needs no physical fields:

```toml
[[layer]]
name        = "m2"
layer_num   = 8
datatype    = 0
fill_color  = "#0044CC"
frame_color = "#0022AA"
# thickness_nm and material inherited from [stack.defaults]
```

---

## Worked example: ASAP7-like stack

![ASAP7 stack cross-section](asap7_stack.svg)

The following TOML uses an anchor, alignment keywords for the parallel contact
group, and downward accumulation for the substrate.

```toml
[stack]
tech_name = "ASAP7-example"
version   = "1.0.0"

[stack.defaults]
thickness_nm = 36.0

# -- Above bonding plane (top of stack -- listed first) -----------------------

[[layer]]
name         = "m1"
layer_num    = 8
datatype     = 0
fill_color   = "#0055FF"
frame_color  = "#0033CC"
# accumulated upward; resolves to z=[139, 175] nm
material     = "metal"

[[layer]]
name         = "v0"
layer_num    = 7
datatype     = 0
fill_color   = "#888888"
frame_color  = "#555555"
# accumulated upward; resolves to z=[103, 139] nm
material     = "tungsten"

[[layer]]
name         = "m0"
layer_num    = 6
datatype     = 0
fill_color   = "#006600"
frame_color  = "#004400"
# accumulated upward; resolves to z=[67, 103] nm
thickness_nm = 36.0
material     = "metal"

[[layer]]
name            = "gate_contact"
layer_num       = 5
datatype        = 0
fill_color      = "#8800FF"
frame_color     = "#6600CC"
align_bottom_to = "poly:top"    # bottom snaps to poly's top = 22nm
thickness_nm    = 45.0          # z=[22, 67] nm
material        = "tungsten"

[[layer]]
name            = "epi_contact"
layer_num       = 4
datatype        = 0
fill_color      = "#FF8800"
frame_color     = "#CC6600"
align_bottom_to = "poly:top"    # same -> parallel group with gate_contact
thickness_nm    = 37.0          # different thickness; z=[22, 59] nm
material        = "tungsten"
# high_water = max(67, 59) = 67 after parallel group

[[layer]]
name         = "poly"
layer_num    = 3
datatype     = 0
fill_color   = "#FFAA00"
frame_color  = "#CC8800"
z_start_nm   = 0.0              # ANCHOR: poly's bottom = bonding plane
thickness_nm = 22.0
material     = "poly"           # z=[0, 22] nm

# -- Below bonding plane (bottom of stack -- listed last) ---------------------

[[layer]]
name         = "substrate"
layer_num    = 0
datatype     = 0
purpose      = "drawing"
fill_color   = "#888888"
frame_color  = "#555555"
# z_start_nm omitted -> accumulated downward from anchor
thickness_nm = 300.0
material     = "silicon"        # resolves to z=[-300, 0] nm
```

### How high_water advances through this stack

![high_water accumulation](high_water_flow.svg)

---

## Order sensitivity

**The order of `[[layer]]` stanzas matters** for accumulated layers.

A layer without `z_start_nm` or alignment keywords takes its z position from
the `high_water` of the layers below it in the file. Moving it changes its z
position. Layers with explicit `z_start_nm` or `align_*` keywords are
order-independent.

```
Safe to reorder:   layers with explicit z_start_nm or align_* keywords
Order-sensitive:   layers without z_start_nm and without align_* keywords
```

A TOML file generated by `gks generate` or `gks import` always emits explicit
`z_start_nm` for every layer, so generated files are safe to reorder freely.
Only hand-authored sparse TOMLs are order-sensitive.

---

## Validation modes

```bash
gks validate stack.toml --for lyp      # display properties only
gks validate stack.toml --for 3d       # physical properties
gks validate stack.toml --for full     # everything (default)
gks validate stack.toml --for full --verbose  # include INFO diagnostics
```

| Severity | Meaning |
|----------|---------|
| ERROR    | Output cannot be generated; must fix |
| WARN     | Suspicious but allowed (gap, burial, thickness mismatch) |
| INFO     | Informational (accumulation trace, parallel groups, anchor) |

---

## Quick reference

| Situation | What to write |
|-----------|--------------|
| Normal sequential layer | `thickness_nm` only; omit `z_start_nm` |
| Explicit z position | `z_start_nm` + `thickness_nm` |
| Parallel group (preferred) | `align_bottom_to = "ref:top"` + `thickness_nm` on each member |
| Parallel group (alternate) | same `z_start_nm` on both members |
| Snap my bottom to a reference edge | `align_bottom_to = "name:top"` or `"name:bottom"` |
| Snap my top to a reference edge | `align_top_to = "name:top"` or `"name:bottom"` |
| Fully constrained (both edges locked) | both `align_*` keywords; omit `thickness_nm` |
| Mark the bonding plane | `z_start_nm = 0.0` on the anchor layer |
| Layer below bonding plane (auto) | last in file; omit `z_start_nm` (accumulates down) |
| Layer below bonding plane (explicit) | `z_start_nm = -300.0` |
| Same thickness as default | omit `thickness_nm` entirely |
| Display-only layer (no 3D) | omit all physical fields |
| Top of stack | first `[[layer]]` in file |
| Bottom of stack | last `[[layer]]` in file |
