# flux_engine.h — Drop-In Constraint Engine

Single-header C99 library combining constraint checking, graph fracture, sediment correction, and 10 domain presets.

## How It Works

This is the complete flux constraint system in one file: check values against bounds, fracture independent constraints into parallel blocks, and layer corrections via sediment. Three stages, one header.

### The 3-Line Integration Story

```c
#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"

// That's it. No dependencies beyond libc + libm.
```

This is the [stb-style single-header pattern](https://github.com/nothings/stb). The header acts as both the public API (types and function declarations) and the implementation. Include it without `FLUX_ENGINE_IMPLEMENTATION` to get declarations only — put that in multiple files. Define it in exactly one `.c` file to emit the implementation. No build system changes, no CMake, no linker flags. Drop the file in your project and compile.

### The Three Stages

**Check** — Given N values and N `(lo, hi)` bounds, produce an error bitmask. Bit `i` is set if value `i` violates its bounds. `NaN` always violates. Boundary values pass (`<=`).

**Fracture** — Build a bipartite graph (constraints × dimensions), find connected components via BFS. Each component is an independent block. Check blocks in parallel, coalesce with bitwise OR. Provably zero false negatives.

**Sediment** — Stack immutable correction layers on top of the base constraints. "Widen coolant temp to [-50, 160] after sensor upgrade" becomes a sediment layer. The base constraint never changes — corrections accumulate, and you always know what was overridden and why.

```c
#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"
#include <stdio.h>

int main(void) {
    /* Load automotive preset: 8 constraints */
    FluxConstraint c[8];
    int n = flux_preset_automotive(c);

    /* Check a single value */
    float values[8] = {95.0f, 3.5f, 3500.0f, 14.1f, 40.0f, 100.0f, 12.0f, 500.0f};
    uint8_t mask = flux_check(values, c, n);

    /* Which constraints failed? */
    for (int i = 0; i < n; i++)
        if (mask & (1 << i))
            printf("FAIL: %s\n", c[i].name);

    return 0;
}
```

## What stb-Style Headers Teach About Dependency Management

The single-header pattern is a design philosophy, not just a packaging trick. It teaches you something about how constraint systems (and libraries in general) should relate to their host programs:

- **Zero integration cost.** No package manager, no build system, no linker flags. Copy one file. This matters for constraint systems because they often end up in environments where adding a dependency is expensive — firmware, embedded systems, game engine plugins, kernel modules. The constraint library should be easier to add than to re-implement.
- **Implementation-on-demand.** The preprocessor emits implementation code exactly once, at the translation unit where you want it. Every other file sees only declarations. This is the C equivalent of "header-only" in C++ — but without the template bloat. One copy of each function exists in the final binary.
- **Single compilation unit.** Because everything is in one header, the compiler can see all the code at once and inline aggressively. `flux_check` on a fixed-size array with known bounds? The optimizer can unroll the entire loop. This is why single-header libraries often benchmark faster than their multi-file equivalents.
- **Fork-friendly.** One file means you can understand the entire library by reading top to bottom. Need to modify the BFS? It's right there. No jumping between 15 source files. For a constraint system that you might need to adapt to exotic hardware, this readability is a feature.

The constraint system architecture maps naturally to the single-header pattern: small, self-contained, performance-critical, and likely to end up in constrained environments.

## What's Inside

| Module | Functions | What |
|--------|-----------|------|
| **Check** | `flux_check`, `flux_check_batch` | NaN-safe bounds checking, zero false negatives |
| **Fracture** | `flux_fracture`, `flux_coalesce` | BFS connected components on bipartite constraint-dim graph |
| **Sediment** | `flux_sediment_init/add/apply` | Progressive constraint tightening via layered overrides |
| **Presets** | `flux_preset_*` | 10 domains: automotive, aviation, medical, energy, robotics, marine, HVAC, manufacturing, telecom, spacecraft |

## Types

```c
typedef struct {
    char name[32]; float lo, hi; FluxSeverity severity;
} FluxConstraint;

typedef struct {
    uint8_t error_mask; uint8_t violated_count; FluxSeverity severity; bool passed;
} FluxResult;
```

## Invariants

- **NaN always violates** — no opt-in, no exceptions
- **Bounds are inclusive** — `<=` and `>=`
- **Zero false negatives** — if a value is out of range, it's always detected
- **Error mask** — bit `i` set means constraint `i` violated

## Build & Test

```bash
make run-test    # 36 tests
make run-bench   # throughput benchmark
```

Or manually:
```bash
gcc -O2 -lm -o test test_flux_engine.c
./test
```

## Performance

Single-file, no external dependencies, compiler can see everything for aggressive inlining.

```bash
make run-bench
```

## Where to Go Next

| Repo | Language | What You'll Learn |
|------|----------|-------------------|
| [flux-fracture](https://github.com/SuperInstance/flux-fracture) | Rust | Fracture algorithm with ownership model, zero-cost generics, parallel iterators |
| [flux-fracture-c](https://github.com/SuperInstance/flux-fracture-c) | C | Standalone fracture library (same BFS, separate repo for minimal deps) |
| [flux-check-js](https://github.com/SuperInstance/flux-check-js) | TypeScript | Full engine with fracture + sediment + industry presets + CLI |
| [plato-types](https://github.com/SuperInstance/plato-types) | Python | Tile lifecycle and Lamport clocks for fleet coordination |
| [tensor-spline](https://github.com/SuperInstance/tensor-spline) | Python | SplineLinear compression for micro models |

## License

MIT. Forge away.
