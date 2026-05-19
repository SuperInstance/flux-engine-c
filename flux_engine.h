/**
 * flux_engine.h — Unified Constraint Engine (Check + Fracture + Sediment + Presets)
 *
 * Single-header C99 library. Combines constraint checking, graph fracture,
 * sediment correction, and domain presets into one drop-in file.
 *
 * INVARIANT: A value outside bounds is ALWAYS detected. No exceptions.
 * NaN always violates all constraints. No opt-in.
 *
 * Usage:
 *   #define FLUX_ENGINE_IMPLEMENTATION
 *   #include "flux_engine.h"
 *
 * Author: Forgemaster ⚒️ (Constraint Theory Ecosystem)
 */

#ifndef FLUX_ENGINE_H
#define FLUX_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Constants                                                           */
/* ================================================================== */

#define FLUX_MAX_CONSTRAINTS  8
#define FLUX_MAX_BLOCKS      32
#define FLUX_SEDIMENT_DEPTH  50
#define FLUX_MAX_DIMS        32

/* ================================================================== */
/* Severity                                                            */
/* ================================================================== */

typedef enum {
    FLUX_SEV_PASS     = 0,
    FLUX_SEV_CAUTION  = 1,
    FLUX_SEV_WARNING  = 2,
    FLUX_SEV_CRITICAL = 3,
} FluxSeverity;

/* ================================================================== */
/* Types                                                               */
/* ================================================================== */

/** Single constraint: value must be in [lo, hi]. */
typedef struct {
    char  name[32];
    float lo;
    float hi;
    FluxSeverity severity;
} FluxConstraint;

/** Result of checking one value against constraints. */
typedef struct {
    uint8_t     error_mask;     /**< Bit i=1 → constraint i violated  */
    uint8_t     violated_count;
    FluxSeverity severity;
    bool        passed;         /**< true iff error_mask == 0         */
} FluxResult;

/** One sediment correction layer. */
typedef struct {
    int   constraint_idx;
    float corrected_lo;
    float corrected_hi;
} FluxSedimentLayer;

/** Stack of sediment layers applied progressively. */
typedef struct {
    FluxSedimentLayer layers[FLUX_SEDIMENT_DEPTH];
    int depth;
    int active_count;
} FluxSedimentStack;

/** Bipartite constraint-dimension graph (flat adjacency). */
typedef struct {
    uint8_t *adj;          /**< Flat (n_constraints × n_dims), row-major */
    int      n_constraints;
    int      n_dims;
} FluxGraph;

/** One independent block from fracture. */
typedef struct {
    int *constraint_indices;  /**< Sorted constraint indices */
    int  n_constraints;
    int *dim_indices;         /**< Sorted dimension indices   */
    int  n_dims;
} FluxBlock;

/** Result of fracturing a system. */
typedef struct {
    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int       n_blocks;
    int       largest_block;
    double    speedup_potential;
} FluxFractureResult;

/* ================================================================== */
/* Core: Check                                                         */
/* ================================================================== */

/** Check a single value against n constraints. Returns error mask (0=pass). */
uint8_t flux_check(float value, const FluxConstraint *constraints, int n);

/** Check an array of values, each against the same constraint set. */
void flux_check_batch(const float *values, int n_values,
                      const FluxConstraint *constraints, int n_constraints,
                      uint8_t *out_masks);

/** Decode a mask into a FluxResult. */
FluxResult flux_result_decode(uint8_t error_mask, const FluxConstraint *constraints, int n);

/* ================================================================== */
/* Fracture                                                            */
/* ================================================================== */

/** Build a graph from dimension masks. dim_masks[i] is an array of dim indices
    for constraint i; mask_lens[i] is its length. */
void flux_graph_init(FluxGraph *g, int n_constraints, int n_dims);
void flux_graph_free(FluxGraph *g);

void flux_graph_from_masks(FluxGraph *g,
                           const int **dim_masks, const int *mask_lens,
                           int n_constraints, int n_dims);

/** Fracture a graph into independent blocks via BFS connected components. */
int flux_fracture(const FluxGraph *graph, FluxBlock *blocks_out, int n_blocks_out);

/** Coalesce block-level error masks via bitwise OR. Returns unified mask. */
uint8_t flux_coalesce(const uint8_t *masks, int n_masks);

/* ================================================================== */
/* Sediment                                                            */
/* ================================================================== */

void flux_sediment_init(FluxSedimentStack *stack);
void flux_sediment_add(FluxSedimentStack *stack, int constraint_idx,
                       float corrected_lo, float corrected_hi);

/** Apply sediment corrections: for each active layer, override the matching
    constraint bounds before checking. mask_out receives the error mask. */
void flux_sediment_apply(FluxSedimentStack *stack,
                         const float *values, int n,
                         const FluxConstraint *base_constraints, int n_constraints,
                         uint8_t *mask_out);

/* ================================================================== */
/* Presets                                                             */
/* ================================================================== */

int flux_preset_automotive(FluxConstraint *out);
int flux_preset_aviation(FluxConstraint *out);
int flux_preset_medical(FluxConstraint *out);
int flux_preset_energy(FluxConstraint *out);
int flux_preset_robotics(FluxConstraint *out);
int flux_preset_marine(FluxConstraint *out);
int flux_preset_hvac(FluxConstraint *out);
int flux_preset_manufacturing(FluxConstraint *out);
int flux_preset_telecom(FluxConstraint *out);
int flux_preset_spacecraft(FluxConstraint *out);

#ifdef __cplusplus
}
#endif

#endif /* FLUX_ENGINE_H */


/* ================================================================== */
/* ========================== IMPLEMENTATION ========================= */
/* ================================================================== */

#ifdef FLUX_ENGINE_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Severity lookup                                                     */
/* ------------------------------------------------------------------ */

static const uint8_t _flux_sev_table[9] = {
    FLUX_SEV_PASS, FLUX_SEV_CAUTION, FLUX_SEV_CAUTION,
    FLUX_SEV_WARNING, FLUX_SEV_WARNING,
    FLUX_SEV_CRITICAL, FLUX_SEV_CRITICAL, FLUX_SEV_CRITICAL, FLUX_SEV_CRITICAL,
};

/* ------------------------------------------------------------------ */
/* Core: Check                                                         */
/* ------------------------------------------------------------------ */

uint8_t flux_check(float value, const FluxConstraint *constraints, int n) {
    /* NaN violates all constraints */
    if (value != value) {
        return (n >= 8) ? 0xFF : (uint8_t)((1u << n) - 1u);
    }
    uint8_t mask = 0;
    for (int i = 0; i < n && i < FLUX_MAX_CONSTRAINTS; i++) {
        if (!(value >= constraints[i].lo && value <= constraints[i].hi)) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

void flux_check_batch(const float *values, int n_values,
                      const FluxConstraint *constraints, int n_constraints,
                      uint8_t *out_masks) {
    for (int i = 0; i < n_values; i++) {
        out_masks[i] = flux_check(values[i], constraints, n_constraints);
    }
}

FluxResult flux_result_decode(uint8_t error_mask, const FluxConstraint *constraints, int n) {
    FluxResult r;
    r.error_mask = error_mask;
    r.passed = (error_mask == 0);

    /* popcount */
    uint8_t count = 0;
    uint8_t m = error_mask;
    while (m) { count++; m &= (uint8_t)(m - 1); }
    r.violated_count = count;

    /* Severity: max severity of violated constraints */
    FluxSeverity sev = FLUX_SEV_PASS;
    for (int i = 0; i < n && i < FLUX_MAX_CONSTRAINTS; i++) {
        if (error_mask & (1u << i)) {
            if (constraints[i].severity > sev)
                sev = constraints[i].severity;
        }
    }
    r.severity = sev;
    return r;
}

/* ------------------------------------------------------------------ */
/* Graph                                                               */
/* ------------------------------------------------------------------ */

void flux_graph_init(FluxGraph *g, int n_constraints, int n_dims) {
    g->n_constraints = n_constraints;
    g->n_dims = n_dims;
    size_t sz = (size_t)n_constraints * (size_t)n_dims;
    g->adj = (uint8_t *)calloc(sz, sizeof(uint8_t));
}

void flux_graph_free(FluxGraph *g) {
    free(g->adj);
    g->adj = NULL;
    g->n_constraints = 0;
    g->n_dims = 0;
}

void flux_graph_from_masks(FluxGraph *g,
                           const int **dim_masks, const int *mask_lens,
                           int n_constraints, int n_dims) {
    flux_graph_init(g, n_constraints, n_dims);
    for (int i = 0; i < n_constraints; i++) {
        for (int j = 0; j < mask_lens[i]; j++) {
            int d = dim_masks[i][j];
            if (d >= 0 && d < n_dims) {
                g->adj[i * n_dims + d] = 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Fracture (BFS connected components)                                 */
/* ------------------------------------------------------------------ */

/* Internal: dynamic int vector */
typedef struct { int *data; int len; int cap; } _fe_intvec;
static void _iv_init(_fe_intvec *v) { v->data = NULL; v->len = 0; v->cap = 0; }
static void _iv_push(_fe_intvec *v, int val) {
    if (v->len >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->data = (int *)realloc(v->data, (size_t)v->cap * sizeof(int));
    }
    v->data[v->len++] = val;
}
static void _iv_free(_fe_intvec *v) { free(v->data); v->data = NULL; v->len = 0; v->cap = 0; }

static int _fe_int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

/* BFS node: tagged union for bipartite traversal */
typedef struct { uint8_t type; int idx; } _fe_bfs_node;

int flux_fracture(const FluxGraph *graph, FluxBlock *blocks_out, int n_blocks_out) {
    int nc = graph->n_constraints;
    int nd = graph->n_dims;
    if (nc == 0) return 0;

    uint8_t *vis_c = (uint8_t *)calloc((size_t)nc, 1);
    uint8_t *vis_d = (uint8_t *)calloc((size_t)nd, 1);
    int qcap = nc + nd + 1;
    _fe_bfs_node *queue = (_fe_bfs_node *)malloc((size_t)qcap * sizeof(_fe_bfs_node));

    int n_blocks = 0;

    for (int seed = 0; seed < nc; seed++) {
        if (vis_c[seed]) continue;

        _fe_intvec comp_c, comp_d;
        _iv_init(&comp_c);
        _iv_init(&comp_d);

        int head = 0, tail = 0;
        queue[tail++] = (_fe_bfs_node){'c', seed};

        while (head < tail) {
            _fe_bfs_node nd_ = queue[head++];
            if (nd_.type == 'c') {
                if (vis_c[nd_.idx]) continue;
                vis_c[nd_.idx] = 1;
                _iv_push(&comp_c, nd_.idx);
                for (int d = 0; d < nd; d++) {
                    if (graph->adj[nd_.idx * nd + d] && !vis_d[d])
                        queue[tail++] = (_fe_bfs_node){'d', d};
                }
            } else {
                if (vis_d[nd_.idx]) continue;
                vis_d[nd_.idx] = 1;
                _iv_push(&comp_d, nd_.idx);
                for (int c = 0; c < nc; c++) {
                    if (graph->adj[c * nd + nd_.idx] && !vis_c[c])
                        queue[tail++] = (_fe_bfs_node){'c', c};
                }
            }
        }

        /* Sort */
        if (comp_c.len > 1) qsort(comp_c.data, (size_t)comp_c.len, sizeof(int), _fe_int_cmp);
        if (comp_d.len > 1) qsort(comp_d.data, (size_t)comp_d.len, sizeof(int), _fe_int_cmp);

        if (n_blocks < n_blocks_out) {
            blocks_out[n_blocks].constraint_indices = comp_c.data;
            blocks_out[n_blocks].n_constraints      = comp_c.len;
            blocks_out[n_blocks].dim_indices         = comp_d.data;
            blocks_out[n_blocks].n_dims              = comp_d.len;
            n_blocks++;
        } else {
            _iv_free(&comp_c);
            _iv_free(&comp_d);
        }
    }

    /* Free orphan dims */
    for (int d = 0; d < nd; d++) {
        if (!vis_d[d] && n_blocks < n_blocks_out) {
            int *one_d = (int *)malloc(sizeof(int));
            one_d[0] = d;
            blocks_out[n_blocks].constraint_indices = NULL;
            blocks_out[n_blocks].n_constraints      = 0;
            blocks_out[n_blocks].dim_indices         = one_d;
            blocks_out[n_blocks].n_dims              = 1;
            n_blocks++;
        }
    }

    free(queue);
    free(vis_c);
    free(vis_d);

    return n_blocks;
}

uint8_t flux_coalesce(const uint8_t *masks, int n_masks) {
    uint8_t result = 0;
    for (int i = 0; i < n_masks; i++) {
        result |= masks[i];
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Sediment                                                            */
/* ------------------------------------------------------------------ */

void flux_sediment_init(FluxSedimentStack *stack) {
    memset(stack, 0, sizeof(*stack));
}

void flux_sediment_add(FluxSedimentStack *stack, int constraint_idx,
                       float corrected_lo, float corrected_hi) {
    if (stack->depth >= FLUX_SEDIMENT_DEPTH) return;
    stack->layers[stack->depth].constraint_idx = constraint_idx;
    stack->layers[stack->depth].corrected_lo   = corrected_lo;
    stack->layers[stack->depth].corrected_hi   = corrected_hi;
    stack->depth++;
    stack->active_count = stack->depth;
}

void flux_sediment_apply(FluxSedimentStack *stack,
                         const float *values, int n,
                         const FluxConstraint *base_constraints, int n_constraints,
                         uint8_t *mask_out) {
    /* Build a working copy of constraints with sediment overrides */
    FluxConstraint working[FLUX_MAX_CONSTRAINTS];
    int count = n_constraints < FLUX_MAX_CONSTRAINTS ? n_constraints : FLUX_MAX_CONSTRAINTS;
    memcpy(working, base_constraints, (size_t)count * sizeof(FluxConstraint));

    /* Apply sediment layers */
    for (int i = 0; i < stack->active_count; i++) {
        int idx = stack->layers[i].constraint_idx;
        if (idx >= 0 && idx < count) {
            working[idx].lo = stack->layers[i].corrected_lo;
            working[idx].hi = stack->layers[i].corrected_hi;
        }
    }

    /* Check each value */
    for (int i = 0; i < n; i++) {
        mask_out[i] = flux_check(values[i], working, count);
    }
}

/* ------------------------------------------------------------------ */
/* Presets                                                             */
/* ------------------------------------------------------------------ */

static void _fc_set(FluxConstraint *c, const char *name, float lo, float hi, FluxSeverity sev) {
    strncpy(c->name, name, 31);
    c->name[31] = '\0';
    c->lo = lo;
    c->hi = hi;
    c->severity = sev;
}

int flux_preset_automotive(FluxConstraint *out) {
    _fc_set(&out[0], "engine_rpm",         0,   8000, FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "vehicle_speed_kmh",   0,   300,  FLUX_SEV_WARNING);
    _fc_set(&out[2], "coolant_temp_c",    -40,   150,  FLUX_SEV_CRITICAL);
    _fc_set(&out[3], "throttle_pct",        0,   100,  FLUX_SEV_CAUTION);
    _fc_set(&out[4], "brake_pressure_bar",  0,   200,  FLUX_SEV_CRITICAL);
    _fc_set(&out[5], "steering_angle_deg",-720,   720,  FLUX_SEV_WARNING);
    _fc_set(&out[6], "battery_voltage_v",   9,    16,  FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "fuel_level_pct",      0,   100,  FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_aviation(FluxConstraint *out) {
    _fc_set(&out[0], "altitude_ft",       -1000, 45000, FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "ground_speed_kt",       0,   600,  FLUX_SEV_WARNING);
    _fc_set(&out[2], "heading_deg",        -180,   180,  FLUX_SEV_CAUTION);
    _fc_set(&out[3], "cabin_temp_c",        -55,    70,  FLUX_SEV_WARNING);
    _fc_set(&out[4], "cabin_pressure_kpa",   75,   101,  FLUX_SEV_CRITICAL);
    _fc_set(&out[5], "fuel_flow_pct",         0,   100,  FLUX_SEV_CRITICAL);
    _fc_set(&out[6], "hydraulic_pct",        60,   100,  FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "pitch_deg",           -90,    90,  FLUX_SEV_WARNING);
    return 8;
}

int flux_preset_medical(FluxConstraint *out) {
    _fc_set(&out[0], "body_temp_c",         36.1f, 37.8f, FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "heart_rate_bpm",         60,   100,  FLUX_SEV_CRITICAL);
    _fc_set(&out[2], "spo2_pct",               95,   100,  FLUX_SEV_CRITICAL);
    _fc_set(&out[3], "bp_systolic_mmhg",       80,   120,  FLUX_SEV_WARNING);
    _fc_set(&out[4], "bp_diastolic_mmhg",      60,   100,  FLUX_SEV_WARNING);
    _fc_set(&out[5], "respiratory_rate",       12,    20,  FLUX_SEV_WARNING);
    _fc_set(&out[6], "ph",                   7.35f, 7.45f, FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "glucose_mg_dl",           0,   300,  FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_energy(FluxConstraint *out) {
    _fc_set(&out[0], "grid_freq_hz",       49.0f, 51.0f, FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "voltage_pu",          0.9f,  1.1f,  FLUX_SEV_CRITICAL);
    _fc_set(&out[2], "transformer_temp_c",     0,    80,   FLUX_SEV_WARNING);
    _fc_set(&out[3], "line_load_pct",          0,   100,   FLUX_SEV_CAUTION);
    _fc_set(&out[4], "current_a",              0,   500,   FLUX_SEV_WARNING);
    _fc_set(&out[5], "power_factor_offset", -100,   100,   FLUX_SEV_CAUTION);
    _fc_set(&out[6], "phase_angle_deg",        0,   360,   FLUX_SEV_CAUTION);
    _fc_set(&out[7], "thd_pct",                0,    50,   FLUX_SEV_WARNING);
    return 8;
}

int flux_preset_robotics(FluxConstraint *out) {
    _fc_set(&out[0], "joint_angle_deg",   -180,  180,  FLUX_SEV_WARNING);
    _fc_set(&out[1], "joint_velocity_dps", -360,  360,  FLUX_SEV_CAUTION);
    _fc_set(&out[2], "torque_nm",           -50,   50,  FLUX_SEV_CRITICAL);
    _fc_set(&out[3], "motor_temp_c",         10,   80,  FLUX_SEV_WARNING);
    _fc_set(&out[4], "battery_soc_pct",       5,  100,  FLUX_SEV_CRITICAL);
    _fc_set(&out[5], "payload_kg",            0,   25,  FLUX_SEV_CRITICAL);
    _fc_set(&out[6], "gripper_force_n",       0,  150,  FLUX_SEV_WARNING);
    _fc_set(&out[7], "imu_accel_g",          -4,    4,  FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_marine(FluxConstraint *out) {
    _fc_set(&out[0], "engine_rpm",          0,   3000,  FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "water_depth_m",       0,   12000, FLUX_SEV_WARNING);
    _fc_set(&out[2], "salinity_psu",        30,    40,   FLUX_SEV_CAUTION);
    _fc_set(&out[3], "water_temp_c",       -2,    35,    FLUX_SEV_CAUTION);
    _fc_set(&out[4], "wave_height_m",       0,    15,    FLUX_SEV_WARNING);
    _fc_set(&out[5], "wind_speed_kt",       0,    100,   FLUX_SEV_WARNING);
    _fc_set(&out[6], "hull_stress_mpa",     0,    350,   FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "bilge_level_pct",     0,    80,    FLUX_SEV_CRITICAL);
    return 8;
}

int flux_preset_hvac(FluxConstraint *out) {
    _fc_set(&out[0], "supply_air_temp_c",  12,    30,   FLUX_SEV_WARNING);
    _fc_set(&out[1], "return_air_temp_c",  18,    28,   FLUX_SEV_CAUTION);
    _fc_set(&out[2], "humidity_pct",       30,    60,   FLUX_SEV_CAUTION);
    _fc_set(&out[3], "co2_ppm",           400,  1000,   FLUX_SEV_WARNING);
    _fc_set(&out[4], "duct_pressure_pa",    0,   500,   FLUX_SEV_CAUTION);
    _fc_set(&out[5], "airflow_cfm",       200,  2000,   FLUX_SEV_WARNING);
    _fc_set(&out[6], "chiller_temp_c",      4,    12,   FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "filter_dp_pa",        0,   250,   FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_manufacturing(FluxConstraint *out) {
    _fc_set(&out[0], "spindle_rpm",           0,  15000,  FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "feed_rate_mm_min",       0,  5000,   FLUX_SEV_WARNING);
    _fc_set(&out[2], "tool_temp_c",           20,   300,   FLUX_SEV_CRITICAL);
    _fc_set(&out[3], "vibration_mm_s",         0,    10,    FLUX_SEV_WARNING);
    _fc_set(&out[4], "surface_roughness_um",   0,     6.3f, FLUX_SEV_CAUTION);
    _fc_set(&out[5], "coolant_pressure_bar",   1,    50,    FLUX_SEV_WARNING);
    _fc_set(&out[6], "dimensional_tol_mm",  -0.05f, 0.05f, FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "power_consumption_kw",   0,    75,    FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_telecom(FluxConstraint *out) {
    _fc_set(&out[0], "signal_dbm",          -120,   -40,  FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "snr_db",                10,    60,   FLUX_SEV_WARNING);
    _fc_set(&out[2], "latency_ms",             0,   200,   FLUX_SEV_CAUTION);
    _fc_set(&out[3], "packet_loss_pct",        0,     5,   FLUX_SEV_WARNING);
    _fc_set(&out[4], "jitter_ms",              0,    30,   FLUX_SEV_CAUTION);
    _fc_set(&out[5], "bandwidth_mbps",         1,  10000,  FLUX_SEV_CRITICAL);
    _fc_set(&out[6], "cpu_temp_c",            30,    85,   FLUX_SEV_WARNING);
    _fc_set(&out[7], "power_consumption_w",    5,   500,   FLUX_SEV_CAUTION);
    return 8;
}

int flux_preset_spacecraft(FluxConstraint *out) {
    _fc_set(&out[0], "altitude_km",          150,  36000,  FLUX_SEV_CRITICAL);
    _fc_set(&out[1], "velocity_km_s",          0,     11,   FLUX_SEV_CRITICAL);
    _fc_set(&out[2], "solar_panel_w",          0,  15000,   FLUX_SEV_WARNING);
    _fc_set(&out[3], "battery_voltage_v",     24,     38,   FLUX_SEV_CRITICAL);
    _fc_set(&out[4], "internal_temp_c",       10,     40,   FLUX_SEV_WARNING);
    _fc_set(&out[5], "radiation_rad",          0,     50,   FLUX_SEV_CRITICAL);
    _fc_set(&out[6], "propellant_pct",         5,    100,   FLUX_SEV_CRITICAL);
    _fc_set(&out[7], "comm_signal_dbm",      -130,   -60,   FLUX_SEV_WARNING);
    return 8;
}

#endif /* FLUX_ENGINE_IMPLEMENTATION */
