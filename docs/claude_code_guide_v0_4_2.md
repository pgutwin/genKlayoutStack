# Claude Code session guide — gks v0.4.2 features

## What you are implementing

Two new features in `buildStack()`:

1. **Two-pass z accumulation with explicit z=0 anchor** — a layer with
   `z_start_nm = 0.0` divides the stack; layers above it accumulate upward,
   layers below it accumulate downward with negative z values.

2. **Alignment keywords** — `align_bottom_to` and `align_top_to` snap a
   layer's edges to named reference layers, replacing the implicit
   parallel-group pattern with explicit declared intent.

Both features are fully specified in `docs/genKlayoutStack_spec_delta_v0_4_2.md`.
The base spec is `docs/PROJECT_SPEC.md` (v0.4.1).

---

## How to run these sessions

### Session 1 — implement the features

**Start a fresh `claude` session (not --resume). Use this prompt:**

```
Read the following files in full before writing any code:
  1. CLAUDE.md
  2. docs/PROJECT_SPEC.md
  3. docs/genKlayoutStack_spec_delta_v0_4_2.md

Then read the existing source files:
  4. include/gks/core/LayerStack.hpp
  5. src/core/buildStack.cpp
  6. tests/unit/test_buildStack.cpp

Implement the two features described in the delta spec:

  A. Two-pass z accumulation with z=0 anchor (spec delta Section 5.2)
     - Find anchor layer (z_start_nm == 0.0 explicitly)
     - Pass 1: upward accumulation for layers above anchor in file
     - Pass 2: downward accumulation for layers below anchor in file
     - Backward compat: no anchor → single upward pass (existing behavior)

  B. Alignment resolution (spec delta Section 5.2a)
     - New optional fields on RawLayer: align_bottom_to, align_top_to
     - Parse "layer_name:edge" string format
     - Topological sort + DFS cycle detection
     - Resolve z_start for aligned layers before accumulation passes
     - All error/warning cases per the conflict rules table

  C. TomlReader: parse the two new TOML fields into RawLayer

  D. Validator: add new validation rules per spec delta Section 5.3

  E. test_buildStack.cpp: add all unit tests listed in the delta spec
     Phase 1 additions section

Do not modify any other files. Run ctest after the change and confirm
all tests pass, including existing tests (backward compat).
```

**Stop after ctest is green. Do not proceed to documentation.**

---

### Session 2 — update documentation

Only after Session 1 is complete and ctest passes. Fresh session.

```
Read CLAUDE.md and docs/PROJECT_SPEC.md and
docs/genKlayoutStack_spec_delta_v0_4_2.md.

The features described in the delta spec have already been implemented
in code. Update documentation only — do not touch any .cpp, .hpp, or
CMakeLists.txt files:

  1. docs/PROJECT_SPEC.md — apply all section replacements and additions
     from the delta spec; increment version to v0.4.2; add changelog entry

  2. docs/gks_z_stack_cheatsheet.md — add a new section explaining:
     - the z anchor (z_start_nm = 0.0)
     - align_bottom_to and align_top_to with examples
     - update the worked ASAP7 example to use alignment keywords
       for the parallel contact group instead of matching z_start_nm

  3. CLAUDE.md — update the Z Model section to describe two-pass
     accumulation and the anchor convention

  4. README.md — update the example TOML to show the anchor and one
     alignment keyword

No SVG changes. No code changes. No ctest needed.
```

---

## What to review before merging

After Session 1:

- [ ] Existing tests still pass (backward compat — no anchor = single upward pass)
- [ ] Anchor in middle of stack: layers above have positive z, below have negative z
- [ ] Parallel group via `align_bottom_to` works; unequal thickness no longer causes false burial warning
- [ ] Circular reference caught as ERROR, not a crash
- [ ] Both alignments + thickness mismatch emits WARN, not ERROR
- [ ] `align_* + z_start_nm` on same layer emits ERROR

After Session 2:

- [ ] Spec version is v0.4.2
- [ ] Cheat sheet worked example uses `align_bottom_to` for contacts, not matching z_start_nm
- [ ] CLAUDE.md z model section reflects two-pass design
- [ ] README example shows anchor layer

---

## Things Claude Code must not do

- Do not change the TOML file ordering convention (top of file = top of
  stack). This is already implemented and tested.
- Do not change the `TomlWriter` — it always emits explicit resolved
  `z_start_nm`, never alignment keywords (OQ6, deferred to v2).
- Do not modify any writer or reader other than `TomlReader` (which needs
  the two new fields).
- Do not start on Phase 2 or later phases.
