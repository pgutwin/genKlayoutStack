# Project: <Working Name>

## 0. Meta & Status

- **Owner:** <your name>
- **Doc status:** Draft / In progress / Stable
- **Last updated:** <YYYY-MM-DD>
- **Change log:**
  - <YYYY-MM-DD> – Initial draft
  - <YYYY-MM-DD> – Updated <section> with <change>

---

## 1. Project Overview

### 1.1 Problem Statement

What problem does this project solve? For whom? In what context?

> One or two paragraphs that someone technical can read and say “got it.”

### 1.2 Goals

Concrete, testable goals.

- G1: <e.g., Parse X, Y, Z formats into a unified IR>
- G2: <e.g., Provide APIs to run analyses A, B, C>
- G3: <e.g., Support datasets up to N elements within M seconds>

### 1.3 Non-Goals

Stuff you are explicitly **not** doing (this keeps scope creep in check).

- NG1: <e.g., No GUI in v1>
- NG2: <e.g., No distributed / cluster support>

### 1.4 Success Criteria

How do you know this project is “good enough” for v1?

- Performance targets
- Feature coverage
- Quality / test coverage targets

---

## 2. Users, Use Cases & Workflows

### 2.1 Target Users

- **Primary user(s):** <roles, e.g. “library developer”, “EDA researcher”>
- **Secondary user(s):** <if any>

### 2.2 Key Use Cases

For each use case, 3–7 bullet steps is enough.

- **UC1: <Name>**
  - Step 1: User does …
  - Step 2: Tool responds …
  - Output: …

- **UC2: <Name>**
  - …

### 2.3 Example Scenarios

A couple of short, concrete stories that tie everything together.

---

## 3. Architecture Overview

### 3.1 High-Level Diagram (Textual)

Describe the major components and how they interact.

- **Components:**
  - `core/` – core data structures and algorithms
  - `io/` – parsers, writers, formats
  - `cli/` – command-line frontends
  - `bindings/` – language bindings (if any)

- **Interactions:**
  - `cli` → `io` → `core` → `io` (for output)
  - `core` exposes a stable C++ API consumed by CLI and Python bindings

### 3.2 Processes & Data Flow

For each major flow:

- Input → transformations → output
- Where state is stored, loaded, mutated

---

## 4. Data Model & Core Abstractions

This is where you define the “IR” and core types.

### 4.1 Domain Concepts

Plain English description of the domain objects.

- **Entity A** – what it represents, key properties
- **Entity B** – …
- **Relationships** – how A relates to B, etc.

### 4.2 Core Data Structures

For each important type:

- Name
- Responsibility
- Fields
- Invariants (must always be true)
- Ownership / lifetime model

Example format:

```text
Type: Circuit
Responsibility: Immutable logical view of a circuit graph.

Fields:
- std::string name
- std::vector<Net> nets
- std::vector<Device> devices

Invariants:
- Net IDs are unique within a Circuit.
- All device pins reference valid nets by ID.

Notes:
- Created by parsers, then passed by const reference to algorithms.
```

### 4.3 Persistence & Serialization

- What gets saved/loaded (JSON? protobuf? custom?)
- Versioning strategy, if any.

---

## 5. Algorithms & Pipelines

For each non-trivial algorithm or pipeline:

### 5.1 Algorithm List

- A1: <Name> – <1-line summary>
- A2: <Name> – <1-line summary>
- …

### 5.2 Algorithm Template

Repeat this subsection per algorithm.

**A#: <Algorithm Name>**

- **Purpose:** What problem does it solve?
- **Inputs:** Types, constraints, preconditions.
- **Outputs:** Types, semantics, postconditions.
- **Complexity:** Rough time/space complexity expectations.
- **High-level description:**
  - Step 1: …
  - Step 2: …
- **Pseudo-code:**

```text
function Foo(input: X) -> Y:
    ...
```

- **Edge cases / corner cases:** enumerate them.
- **Testing strategy:** what must be unit-tested vs integration-tested.

---

## 6. Codebase Layout & Module Boundaries

### 6.1 Directory Structure

Planned source layout, e.g.:

```text
project_root/
  CMakeLists.txt
  include/
    project/
      core.hpp
      io.hpp
  src/
    core/
      ...
    io/
      ...
    cli/
      ...
  tests/
    unit/
    integration/
  docs/
    PROJECT_SPEC.md
```

### 6.2 Modules & Boundaries

Define logical modules + rules:

- **Module `core`**
  - Responsibilities: core types and algorithms
  - May depend on: standard library, small utility module
  - Must **not** depend on: CLI, bindings, heavy external deps

- **Module `io`**
  - Responsibilities: parsing/writing external formats
  - Depends on: `core`

- **Module `cli`**
  - Responsibilities: command-line UX
  - Depends on: `core`, `io`

Explicitly stating “this module must not depend on X” saves you from a rat’s nest later.

### 6.3 Naming Conventions

- File names, namespaces, class names
- Error handling strategy (exceptions vs status codes vs `expected<>`)

---

## 7. External Dependencies & Tooling

### 7.1 Languages & Versions

- **Primary language:** <e.g., C++20>
- **Build system:** <e.g., CMake + Ninja>
- **Supported platforms:** <e.g., macOS 14, Linux x86_64>

### 7.2 Libraries

For each library:

- Name, version/pin strategy
- Why it’s needed / what problems it solves
- How it’s integrated (system package, submodule, FetchContent, etc.)

Example:

- **PEGTL 3.2.x**
  - Purpose: Parsing DSL input files
  - Integration: vendored under `external/PEGTL`

### 7.3 Testing & QA

- Unit test framework
- Where tests live
- How tests are run (commands)
- Coverage targets (if you care)

---

## 8. LLM Collaboration Plan (ChatGPT Usage)

### 8.1 Role of ChatGPT in This Project

- What you will ask it to do:
  - Draft data structures consistent with this spec
  - Generate boilerplate code / glue
  - Propose algorithms and refine them
  - Write test scaffolding, etc.
- What you **will not** rely on it for:
  - Silent design changes that contradict this spec
  - Magical performance claims without benchmarks

### 8.2 Working Protocol

- **Source of truth:** This document + your actual repo, not whatever the model “remembers.”
- **Update cycle:**
  - You update this spec when design decisions change.
  - When asking for code changes, you paste *just* the relevant parts + the spec snippets that apply.
- **Patch format & rules:**  
  - Unified diff, filenames without paths / or with specific paths.
  - No stubs; code must compile.
  - etc. (spell out your rules here.)

### 8.3 Prompt Cookbook

A few standard prompt patterns you’ll reuse:

- “Given the `Data Model` section and the following header, generate implementation for X.”
- “Based on `Algorithm A#` in the spec, implement it in file Y with this signature.”
- “Propose GTests for class Z given these invariants.”

This section is mostly for your future self so you don’t reinvent the wheel every session.

---

## 9. Roadmap & Milestones

### 9.1 Phases

Break the project into phases where each phase can reasonably compile and run something.

Example:

1. **Phase 0 – Skeleton**
   - Repo layout, CMake, blank modules, CI smoke test.
2. **Phase 1 – Core Data Model**
   - Implement core types + basic tests.
3. **Phase 2 – Parsing I/O**
   - Parsers for input format A, basic round-trip tests.
4. **Phase 3 – Core Algorithms**
   - Implement A1, A2 …
5. **Phase 4 – CLI & UX**
   - Command-line interface wrapping core functionality.

### 9.2 Milestones

Under each phase:

- Tasks
- Deliverables
- Dependencies

---

## 10. Open Questions, Risks & Parking Lot

### 10.1 Open Questions

Explicit list of things you haven’t decided yet.

- OQ1: <question>
- OQ2: <question>

### 10.2 Risks

- Technical risks (performance, complexity)
- Process risks (LLM confusion, scope creep)

### 10.3 Parking Lot

Ideas you don’t want to think about right now but also don’t want to forget.

---

## 11. Appendices

### 11.1 Glossary

Define domain-specific terms and abbreviations.

### 11.2 References

Papers, repos, docs you are aligning with.

### 11.3 Design Alternatives

Brief notes on approaches considered and rejected, and why.

