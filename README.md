# flux_engine.h — Drop-In Constraint Engine

Single-header C99 library combining constraint checking, graph fracture, sediment correction, and 10 domain presets.

## 3-Line Integration

```c
#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"

// That's it. No dependencies beyond libc + libm.
```

## Quick Start

```c
#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"
#include <stdio.h>

int main(void) {
    // Load automotive preset
    FluxConstraint c[8];
    int n = flux_preset_automotive(c);

    // Check a value
    float coolant_temp = 95.0f;
    uint8_t mask = flux_check(coolant_temp, c, n);

    if (mask & 0x04)  // bit 2 = coolant_temp_c
        printf("Coolant temp %.1f°C out of range!\n", coolant_temp);

    return 0;
}
```

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

## License

MIT. Forge away.
