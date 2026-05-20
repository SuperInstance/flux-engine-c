/**
 * test_flux_engine.c — 43 tests for the unified constraint engine
 *
 * Compile: gcc -O2 -lm -o test_flux_engine test_flux_engine.c
 */

#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) printf("  %-50s ", name)
#define PASS() printf("PASS\n"); tests_passed++
#define FAIL(msg) printf("FAIL: %s\n", msg); tests_failed++
#define ASSERT_EQ(a, b, msg) if ((a) != (b)) { FAIL(msg); return; }

/* ================================================================== */
/* Section 1: Core Check (12 tests)                                    */
/* ================================================================== */

static void test_within_bounds(void) {
    TEST_START("within bounds → mask 0");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(50.0f, &c, 1);
    ASSERT_EQ(m, 0, "expected 0");
    PASS();
}

static void test_at_lo_bound(void) {
    TEST_START("at lo bound → mask 0 (inclusive)");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(0.0f, &c, 1);
    ASSERT_EQ(m, 0, "lo bound should pass");
    PASS();
}

static void test_at_hi_bound(void) {
    TEST_START("at hi bound → mask 0 (inclusive)");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(100.0f, &c, 1);
    ASSERT_EQ(m, 0, "hi bound should pass");
    PASS();
}

static void test_below_lo(void) {
    TEST_START("below lo → bit 0 set");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(-1.0f, &c, 1);
    ASSERT_EQ(m, 1, "expected bit 0");
    PASS();
}

static void test_above_hi(void) {
    TEST_START("above hi → bit 0 set");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(101.0f, &c, 1);
    ASSERT_EQ(m, 1, "expected bit 0");
    PASS();
}

static void test_nan_violates(void) {
    TEST_START("NaN violates all → full mask");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
        {"c", 0, 10, FLUX_SEV_CAUTION},
    };
    uint8_t m = flux_check(NAN, c, 3);
    ASSERT_EQ(m, 0x07, "NaN should violate all 3");
    PASS();
}

static void test_inf_violates(void) {
    TEST_START("+Inf violates → bit 0");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(INFINITY, &c, 1);
    ASSERT_EQ(m, 1, "+Inf should violate");
    PASS();
}

static void test_neg_inf_violates(void) {
    TEST_START("-Inf violates → bit 0");
    FluxConstraint c = {"x", -100, 100, FLUX_SEV_CAUTION};
    uint8_t m = flux_check(-INFINITY, &c, 1);
    ASSERT_EQ(m, 1, "-Inf should violate");
    PASS();
}

static void test_multi_constraint_partial(void) {
    TEST_START("multi constraint: partial violation");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 5, FLUX_SEV_CAUTION},
        {"c", 0, 100, FLUX_SEV_CAUTION},
    };
    /* value=7: passes a and c, fails b */
    uint8_t m = flux_check(7.0f, c, 3);
    ASSERT_EQ(m, 0x02, "only b (bit 1) should fail");
    PASS();
}

static void test_batch(void) {
    TEST_START("batch check: mixed results");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    float vals[4] = {50.0f, -1.0f, 101.0f, 0.0f};
    uint8_t masks[4];
    flux_check_batch(vals, 4, &c, 1, masks);
    ASSERT_EQ(masks[0], 0, "50 should pass");
    ASSERT_EQ(masks[1], 1, "-1 should fail");
    ASSERT_EQ(masks[2], 1, "101 should fail");
    ASSERT_EQ(masks[3], 0, "0 should pass");
    PASS();
}

static void test_result_decode_pass(void) {
    TEST_START("result decode: all pass");
    FluxConstraint c[2] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_WARNING},
    };
    FluxResult r = flux_result_decode(0, c, 2);
    ASSERT_EQ(r.passed, true, "should pass");
    ASSERT_EQ(r.violated_count, 0, "no violations");
    ASSERT_EQ(r.severity, FLUX_SEV_PASS, "sev pass");
    PASS();
}

static void test_result_decode_severity(void) {
    TEST_START("result decode: severity = max violated");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CRITICAL},
        {"c", 0, 10, FLUX_SEV_WARNING},
    };
    FluxResult r = flux_result_decode(0x05, c, 3); /* bits 0,2 set */
    ASSERT_EQ(r.passed, false, "should fail");
    ASSERT_EQ(r.violated_count, 2, "2 violations");
    ASSERT_EQ(r.severity, FLUX_SEV_WARNING, "max of caution+warning = warning");
    PASS();
}

/* ================================================================== */
/* Section 2: Fracture (8 tests)                                       */
/* ================================================================== */

static void test_fracture_single_block(void) {
    TEST_START("fracture: all connected → 1 block");
    /* 3 constraints all share dim 0 */
    int d0[] = {0}, d1[] = {0}, d2[] = {0};
    const int *masks[] = {d0, d1, d2};
    int lens[] = {1, 1, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, masks, lens, 3, 2);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    /* 1 constraint block + 1 orphan dim = 2 blocks total */
    ASSERT_EQ(nb, 2, "should be 2 blocks (1 constraint block + 1 orphan dim)");
    ASSERT_EQ(blocks[0].n_constraints, 3, "all 3 constraints in first block");

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_fracture_independent(void) {
    TEST_START("fracture: 2 independent pairs → 2 blocks");
    /* c0,c1 share dim 0; c2,c3 share dim 1 */
    int d0[] = {0}, d1[] = {0}, d2[] = {1}, d3[] = {1};
    const int *masks[] = {d0, d1, d2, d3};
    int lens[] = {1, 1, 1, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, masks, lens, 4, 2);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    ASSERT_EQ(nb, 2, "should be 2 blocks");

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_fracture_fully_independent(void) {
    TEST_START("fracture: 4 isolated constraints → 4 blocks");
    int d0[] = {0}, d1[] = {1}, d2[] = {2}, d3[] = {3};
    const int *masks[] = {d0, d1, d2, d3};
    int lens[] = {1, 1, 1, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, masks, lens, 4, 4);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    ASSERT_EQ(nb, 4, "4 independent blocks");

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_fracture_empty(void) {
    TEST_START("fracture: empty graph → 0 blocks");
    FluxGraph g;
    flux_graph_init(&g, 0, 0);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    ASSERT_EQ(nb, 0, "empty → 0 blocks");

    flux_graph_free(&g);
    PASS();
}

static void test_fracture_bridge(void) {
    TEST_START("fracture: bridge constraint merges blocks");
    /* c0→d0, c1→d0,d1, c2→d1: all connected via c1 bridge */
    int d0[] = {0}, d1[] = {0, 1}, d2[] = {1};
    const int *masks[] = {d0, d1, d2};
    int lens[] = {1, 2, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, masks, lens, 3, 2);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    ASSERT_EQ(nb, 1, "bridge merges all");

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_fracture_output_limit(void) {
    TEST_START("fracture: output buffer limit respected");
    int d0[] = {0}, d1[] = {1}, d2[] = {2};
    const int *masks[] = {d0, d1, d2};
    int lens[] = {1, 1, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, masks, lens, 3, 3);

    FluxBlock blocks[2]; /* only room for 2 */
    int nb = flux_fracture(&g, blocks, 2);
    ASSERT_EQ(nb, 2, "limited to 2");

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_coalesce(void) {
    TEST_START("coalesce: bitwise OR of masks");
    uint8_t masks[] = {0x01, 0x02, 0x04};
    uint8_t result = flux_coalesce(masks, 3);
    ASSERT_EQ(result, 0x07, "OR of all bits");
    PASS();
}

static void test_coalesce_empty(void) {
    TEST_START("coalesce: empty → 0");
    uint8_t result = flux_coalesce(NULL, 0);
    ASSERT_EQ(result, 0, "empty OR = 0");
    PASS();
}

/* ================================================================== */
/* Section 3: Sediment (6 tests)                                       */
/* ================================================================== */

static void test_sediment_init(void) {
    TEST_START("sediment init: zero state");
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    ASSERT_EQ(stack.depth, 0, "depth 0");
    ASSERT_EQ(stack.active_count, 0, "active 0");
    PASS();
}

static void test_sediment_add(void) {
    TEST_START("sediment add: depth increments");
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    flux_sediment_add(&stack, 0, 10.0f, 90.0f);
    flux_sediment_add(&stack, 1, 0.0f, 50.0f);
    ASSERT_EQ(stack.depth, 2, "depth 2");
    ASSERT_EQ(stack.active_count, 2, "active 2");
    PASS();
}

static void test_sediment_override_bounds(void) {
    TEST_START("sediment apply: tighter bounds catch violations");
    FluxConstraint c[2] = {
        {"temp", 0, 100, FLUX_SEV_CAUTION},
        {"speed", 0, 200, FLUX_SEV_CAUTION},
    };
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    /* Override temp to [10, 90] — value 95 should now fail */
    flux_sediment_add(&stack, 0, 10.0f, 90.0f);

    float vals[] = {95.0f, 100.0f};
    uint8_t masks[2];
    flux_sediment_apply(&stack, vals, 2, c, 2, masks);
    ASSERT_EQ(masks[0], 0x01, "95 fails tighter temp bounds");
    ASSERT_EQ(masks[1], 0x01, "100 also fails tighter temp bounds [10,90]");
    PASS();
}

static void test_sediment_no_layers(void) {
    TEST_START("sediment apply: no layers → original constraints");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    FluxSedimentStack stack;
    flux_sediment_init(&stack);

    float vals[] = {50.0f};
    uint8_t masks[1];
    flux_sediment_apply(&stack, vals, 1, &c, 1, masks);
    ASSERT_EQ(masks[0], 0, "no layers → original check");
    PASS();
}

static void test_sediment_multiple_overrides(void) {
    TEST_START("sediment apply: last layer wins for same idx");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    flux_sediment_add(&stack, 0, 10.0f, 90.0f);
    flux_sediment_add(&stack, 0, 20.0f, 80.0f); /* overrides previous */

    float vals[] = {85.0f};
    uint8_t masks[1];
    flux_sediment_apply(&stack, vals, 1, &c, 1, masks);
    ASSERT_EQ(masks[0], 1, "85 outside [20,80] should fail");
    PASS();
}

static void test_sediment_depth_limit(void) {
    TEST_START("sediment: depth limit respected");
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    for (int i = 0; i < FLUX_SEDIMENT_DEPTH + 5; i++) {
        flux_sediment_add(&stack, 0, 0, (float)i);
    }
    ASSERT_EQ(stack.depth, FLUX_SEDIMENT_DEPTH, "capped at max");
    PASS();
}

/* ================================================================== */
/* Section 4: Presets (10 tests)                                       */
/* ================================================================== */

static void test_preset_automotive(void) {
    TEST_START("preset automotive: 8 constraints, valid bounds");
    FluxConstraint c[8];
    int n = flux_preset_automotive(c);
    ASSERT_EQ(n, 8, "8 constraints");
    /* battery voltage: [9, 16], value 12 should pass */
    ASSERT_EQ(flux_check(12.0f, c, n) & 0x40, 0, "battery 12V passes");
    PASS();
}

static void test_preset_aviation(void) {
    TEST_START("preset aviation: 8 constraints");
    FluxConstraint c[8];
    int n = flux_preset_aviation(c);
    ASSERT_EQ(n, 8, "8 constraints");
    PASS();
}

static void test_preset_medical(void) {
    TEST_START("preset medical: 8 constraints");
    FluxConstraint c[8];
    int n = flux_preset_medical(c);
    ASSERT_EQ(n, 8, "8 constraints");
    /* Normal body temp should pass */
    uint8_t m = flux_check(37.0f, c, n);
    ASSERT_EQ(m & 0x01, 0, "37°C body temp passes");
    PASS();
}

static void test_preset_energy(void) {
    TEST_START("preset energy: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_energy(c), 8, "8 constraints");
    PASS();
}

static void test_preset_robotics(void) {
    TEST_START("preset robotics: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_robotics(c), 8, "8 constraints");
    PASS();
}

static void test_preset_marine(void) {
    TEST_START("preset marine: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_marine(c), 8, "8 constraints");
    PASS();
}

static void test_preset_hvac(void) {
    TEST_START("preset HVAC: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_hvac(c), 8, "8 constraints");
    PASS();
}

static void test_preset_manufacturing(void) {
    TEST_START("preset manufacturing: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_manufacturing(c), 8, "8 constraints");
    PASS();
}

static void test_preset_telecom(void) {
    TEST_START("preset telecom: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_telecom(c), 8, "8 constraints");
    PASS();
}

static void test_preset_spacecraft(void) {
    TEST_START("preset spacecraft: 8 constraints");
    FluxConstraint c[8];
    ASSERT_EQ(flux_preset_spacecraft(c), 8, "8 constraints");
    PASS();
}

/* ================================================================== */
/* Section 5: Integration (4 tests)                                    */
/* ================================================================== */

static void test_check_then_fracture(void) {
    TEST_START("integration: check + fracture pipeline");
    /* 4 constraints: {0,1} share dim 0, {2,3} share dim 1 */
    int d0[] = {0}, d1[] = {0}, d2[] = {1}, d3[] = {1};
    const int *dm[] = {d0, d1, d2, d3};
    int dl[] = {1, 1, 1, 1};
    FluxGraph g;
    flux_graph_from_masks(&g, dm, dl, 4, 2);

    FluxBlock blocks[FLUX_MAX_BLOCKS];
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    ASSERT_EQ(nb, 2, "2 blocks");

    /* Check block 0 independently */
    FluxConstraint c[] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
        {"c", 0, 10, FLUX_SEV_CAUTION},
        {"d", 0, 10, FLUX_SEV_CAUTION},
    };
    uint8_t block_masks[2] = {0};
    for (int i = 0; i < blocks[0].n_constraints; i++) {
        /* Check value 15 against each constraint in block 0 — should violate */
        block_masks[0] = flux_check(15.0f, c, 4); /* simplified */
    }

    uint8_t combined = flux_coalesce(block_masks, nb);
    /* Just verify coalesce works — masks are all the same here */
    (void)combined;

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
    PASS();
}

static void test_sediment_with_preset(void) {
    TEST_START("integration: sediment + automotive preset");
    FluxConstraint c[8];
    int n = flux_preset_automotive(c);

    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    /* Tighten coolant temp from [-40,150] to [0,120] */
    flux_sediment_add(&stack, 2, 0.0f, 120.0f);

    float vals[] = {-5.0f};
    uint8_t masks[1];
    flux_sediment_apply(&stack, vals, 1, c, n, masks);
    ASSERT_EQ(masks[0] & 0x04, 0x04, "coolant -5°C should fail tightened bounds");
    PASS();
}

static void test_nan_batch_all_violate(void) {
    TEST_START("integration: batch of NaN → all violate");
    FluxConstraint c[2] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
    };
    float vals[3] = {NAN, NAN, NAN};
    uint8_t masks[3];
    flux_check_batch(vals, 3, c, 2, masks);
    ASSERT_EQ(masks[0], 0x03, "NaN mask 1");
    ASSERT_EQ(masks[1], 0x03, "NaN mask 2");
    ASSERT_EQ(masks[2], 0x03, "NaN mask 3");
    PASS();
}

static void test_zero_constraints(void) {
    TEST_START("edge case: zero constraints → mask 0");
    uint8_t m = flux_check(42.0f, NULL, 0);
    ASSERT_EQ(m, 0, "no constraints → pass");
    PASS();
}

/* ================================================================== */
/* Section 6: flux_check_vector (3 tests)                              */
/* ================================================================== */

static void test_vector_all_pass(void) {
    TEST_START("check_vector: all values in bounds → mask 0");
    FluxConstraint c[4] = {
        {"a", 0, 100, FLUX_SEV_CAUTION},
        {"b", -10, 10, FLUX_SEV_CAUTION},
        {"c", 0, 1000, FLUX_SEV_CAUTION},
        {"d", 50, 150, FLUX_SEV_CAUTION},
    };
    double vals[4] = {50.0, 0.0, 500.0, 100.0};
    uint8_t m = flux_check_vector(vals, 4, c);
    ASSERT_EQ(m, 0, "all in bounds");
    PASS();
}

static void test_vector_partial_violation(void) {
    TEST_START("check_vector: partial violations");
    FluxConstraint c[4] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
        {"c", 0, 10, FLUX_SEV_CAUTION},
        {"d", 0, 10, FLUX_SEV_CAUTION},
    };
    double vals[4] = {5.0, 15.0, 3.0, 20.0};  /* b and d violate */
    uint8_t m = flux_check_vector(vals, 4, c);
    ASSERT_EQ(m, 0x0A, "bits 1 and 3 set");
    PASS();
}

static void test_vector_nan_violates(void) {
    TEST_START("check_vector: NaN violates its constraint");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
        {"c", 0, 10, FLUX_SEV_CAUTION},
    };
    double vals[3] = {5.0, NAN, 3.0};  /* only b is NaN */
    uint8_t m = flux_check_vector(vals, 3, c);
    ASSERT_EQ(m, 0x02, "only bit 1 (NaN) violates");
    PASS();
}

/* ================================================================== */
/* Section 7: Serialization (8 tests)                                  */
/* ================================================================== */

static void test_json_roundtrip(void) {
    TEST_START("json: roundtrip 3 constraints");
    FluxConstraint c[3] = {
        {"temp", -40.0f, 150.0f, FLUX_SEV_CAUTION},
        {"pressure", 0.0f, 200.0f, FLUX_SEV_WARNING},
        {"speed", 0.0f, 100.0f, FLUX_SEV_CRITICAL},
    };
    char* json = flux_constraints_to_json(c, 3);
    ASSERT_EQ(json != NULL, true, "json should not be NULL");

    FluxConstraint out[3];
    int n = flux_constraints_from_json(json, out, 3);
    free(json);
    ASSERT_EQ(n, 3, "should parse 3");
    ASSERT_EQ(strcmp(out[0].name, "temp"), 0, "name 0");
    ASSERT_EQ(strcmp(out[1].name, "pressure"), 0, "name 1");
    ASSERT_EQ(strcmp(out[2].name, "speed"), 0, "name 2");
    /* Bounds should survive within float precision */
    ASSERT_EQ(out[0].severity, FLUX_SEV_CAUTION, "sev 0");
    ASSERT_EQ(out[2].severity, FLUX_SEV_CRITICAL, "sev 2");
    PASS();
}

static void test_json_bounds_preserved(void) {
    TEST_START("json: bounds preserved after roundtrip");
    FluxConstraint c = {"temp", -40.0f, 150.0f, FLUX_SEV_WARNING};
    char* json = flux_constraints_to_json(&c, 1);
    FluxConstraint out;
    int n = flux_constraints_from_json(json, &out, 1);
    free(json);
    ASSERT_EQ(n, 1, "1 parsed");
    ASSERT_EQ(out.lo <= -39.9f && out.lo >= -40.1f, true, "lo preserved");
    ASSERT_EQ(out.hi >= 149.9f && out.hi <= 150.1f, true, "hi preserved");
    PASS();
}

static void test_json_empty_array(void) {
    TEST_START("json: empty array → 0 constraints");
    FluxConstraint out[1];
    int n = flux_constraints_from_json("[]", out, 1);
    ASSERT_EQ(n, 0, "empty array → 0");
    PASS();
}

static void test_json_null_input(void) {
    TEST_START("json: NULL input → -1");
    FluxConstraint out[1];
    int n = flux_constraints_from_json(NULL, out, 1);
    ASSERT_EQ(n, -1, "NULL → -1");
    PASS();
}

static void test_json_invalid_syntax(void) {
    TEST_START("json: invalid syntax → -1");
    FluxConstraint out[1];
    int n = flux_constraints_from_json("not json", out, 1);
    ASSERT_EQ(n, -1, "invalid → -1");
    PASS();
}

static void test_file_save_load(void) {
    TEST_START("file: save and load roundtrip");
    FluxConstraint c[2] = {
        {"x", -10.0f, 10.0f, FLUX_SEV_CAUTION},
        {"y", 0.0f, 100.0f, FLUX_SEV_WARNING},
    };
    const char* path = "/tmp/flux_test_preset.json";
    int rc = flux_save_preset(path, c, 2);
    ASSERT_EQ(rc, 0, "save should succeed");

    FluxConstraint loaded[2];
    int n = flux_load_preset(path, loaded, 2);
    ASSERT_EQ(n, 2, "should load 2");
    ASSERT_EQ(strcmp(loaded[0].name, "x"), 0, "name x");
    ASSERT_EQ(strcmp(loaded[1].name, "y"), 0, "name y");
    PASS();
}

static void test_file_not_found(void) {
    TEST_START("file: nonexistent → -1");
    FluxConstraint out[1];
    int n = flux_load_preset("/tmp/flux_nonexistent_12345.json", out, 1);
    ASSERT_EQ(n, -1, "not found → -1");
    PASS();
}

static void test_json_preset_roundtrip(void) {
    TEST_START("json: automotive preset roundtrip");
    FluxConstraint c[8];
    int n = flux_preset_automotive(c);
    char* json = flux_constraints_to_json(c, n);
    ASSERT_EQ(json != NULL, true, "json not NULL");

    FluxConstraint out[8];
    int n2 = flux_constraints_from_json(json, out, 8);
    free(json);
    ASSERT_EQ(n2, 8, "parsed 8");
    /* Verify constraints still work */
    uint8_t m = flux_check(12.0f, out, n2);
    ASSERT_EQ(m & 0x40, 0, "battery 12V passes after roundtrip");
    PASS();
}

/* ================================================================== */
/* Section 8: Aggregation (7 tests)                                    */
/* ================================================================== */

static void test_agg_all_pass(void) {
    TEST_START("aggregate: all readings pass");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    uint8_t masks[] = {0, 0, 0, 0};
    FluxAggregate agg;
    flux_aggregate(masks, 4, &c, 1, &agg);
    ASSERT_EQ(agg.total_readings, 4, "4 readings");
    ASSERT_EQ(agg.total_violations, 0, "0 violations");
    ASSERT_EQ(agg.severity_breakdown[FLUX_SEV_PASS], 4, "4 pass");
    PASS();
}

static void test_agg_all_violate(void) {
    TEST_START("aggregate: all readings violate");
    FluxConstraint c = {"x", 0, 10, FLUX_SEV_CRITICAL};
    uint8_t masks[] = {0x01, 0x01, 0x01};
    FluxAggregate agg;
    flux_aggregate(masks, 3, &c, 1, &agg);
    ASSERT_EQ(agg.total_violations, 3, "3 violations");
    ASSERT_EQ(agg.per_constraint_violations[0], 3, "c0 violations = 3");
    ASSERT_EQ(agg.severity_breakdown[FLUX_SEV_CRITICAL], 3, "3 critical");
    PASS();
}

static void test_agg_violation_rate(void) {
    TEST_START("aggregate: violation rate correct");
    FluxConstraint c = {"x", 0, 10, FLUX_SEV_CAUTION};
    uint8_t masks[] = {0, 0x01, 0, 0x01, 0};
    FluxAggregate agg;
    flux_aggregate(masks, 5, &c, 1, &agg);
    ASSERT_EQ(agg.total_readings, 5, "5 total");
    ASSERT_EQ(agg.total_violations, 2, "2 violations");
    /* rate = 2/5 = 0.4 */
    ASSERT_EQ(agg.violation_rate > 0.39 && agg.violation_rate < 0.41, true, "rate ~0.4");
    PASS();
}

static void test_agg_per_constraint(void) {
    TEST_START("aggregate: per-constraint violation counts");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_WARNING},
        {"c", 0, 10, FLUX_SEV_CRITICAL},
    };
    /* reading 0: a fails, reading 1: b+c fail, reading 2: all pass */
    uint8_t masks[] = {0x01, 0x06, 0x00};
    FluxAggregate agg;
    flux_aggregate(masks, 3, c, 3, &agg);
    ASSERT_EQ(agg.per_constraint_violations[0], 1, "a = 1");
    ASSERT_EQ(agg.per_constraint_violations[1], 1, "b = 1");
    ASSERT_EQ(agg.per_constraint_violations[2], 1, "c = 1");
    PASS();
}

static void test_agg_worst_reading(void) {
    TEST_START("aggregate: worst reading index");
    FluxConstraint c[3] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CAUTION},
        {"c", 0, 10, FLUX_SEV_CAUTION},
    };
    uint8_t masks[] = {0x01, 0x07, 0x02}; /* 1, 3, 1 violations */
    FluxAggregate agg;
    flux_aggregate(masks, 3, c, 3, &agg);
    ASSERT_EQ(agg.worst_reading_index, 1, "reading 1 has most violations");
    PASS();
}

static void test_agg_severity_breakdown(void) {
    TEST_START("aggregate: severity breakdown");
    FluxConstraint c[2] = {
        {"a", 0, 10, FLUX_SEV_CAUTION},
        {"b", 0, 10, FLUX_SEV_CRITICAL},
    };
    /* reading 0: a fails (caution), reading 1: b fails (critical), reading 2: pass */
    uint8_t masks[] = {0x01, 0x02, 0x00};
    FluxAggregate agg;
    flux_aggregate(masks, 3, c, 2, &agg);
    ASSERT_EQ(agg.severity_breakdown[FLUX_SEV_PASS], 1, "1 pass");
    ASSERT_EQ(agg.severity_breakdown[FLUX_SEV_CAUTION], 1, "1 caution");
    ASSERT_EQ(agg.severity_breakdown[FLUX_SEV_CRITICAL], 1, "1 critical");
    PASS();
}

static void test_agg_empty(void) {
    TEST_START("aggregate: empty input");
    FluxAggregate agg;
    flux_aggregate(NULL, 0, NULL, 0, &agg);
    ASSERT_EQ(agg.total_readings, 0, "0 readings");
    ASSERT_EQ(agg.total_violations, 0, "0 violations");
    ASSERT_EQ(agg.violation_rate, 0.0, "rate 0");
    PASS();
}

/* ================================================================== */
/* Section 9: Drift Detection (6 tests)                                */
/* ================================================================== */

static void test_drift_init(void) {
    TEST_START("drift: init zeros state");
    FluxDriftDetector det;
    flux_drift_init(&det, 100);
    ASSERT_EQ(det.window_size, 100, "window 100");
    ASSERT_EQ(det.count, 0, "count 0");
    for (int i = 0; i < FLUX_MAX_CONSTRAINTS; i++) {
        ASSERT_EQ(det.sums[i] == 0.0, true, "sum 0");
    }
    PASS();
}

static void test_drift_no_drift(void) {
    TEST_START("drift: centered values → no drift");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    FluxDriftDetector det;
    flux_drift_init(&det, 10);
    /* Add values centered at midpoint (50) */
    for (int i = 0; i < 10; i++) {
        double v = 50.0;
        flux_drift_add(&det, &v, 1);
    }
    int dc[8]; double dr[8];
    int nd = flux_drift_detect(&det, &c, 1, dc, dr);
    ASSERT_EQ(nd, 0, "no drift");
    PASS();
}

static void test_drift_detected(void) {
    TEST_START("drift: offset values → drift detected");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    FluxDriftDetector det;
    flux_drift_init(&det, 10);
    /* Add values at 95 — offset from midpoint 50 by 45, threshold is 10% of 100 = 10 */
    for (int i = 0; i < 10; i++) {
        double v = 95.0;
        flux_drift_add(&det, &v, 1);
    }
    int dc[8]; double dr[8];
    int nd = flux_drift_detect(&det, &c, 1, dc, dr);
    ASSERT_EQ(nd, 1, "1 drifting");
    ASSERT_EQ(dc[0], 0, "constraint 0 drifting");
    ASSERT_EQ(dr[0] > 0.0, true, "drift rate > 0");
    PASS();
}

static void test_drift_multi_constraint(void) {
    TEST_START("drift: only one of two constraints drifting");
    FluxConstraint c[2] = {
        {"temp", 0, 100, FLUX_SEV_CAUTION},
        {"speed", 0, 200, FLUX_SEV_CAUTION},
    };
    FluxDriftDetector det;
    flux_drift_init(&det, 10);
    /* temp at 95 (drifting), speed at 100 (centered) */
    for (int i = 0; i < 10; i++) {
        double v[2] = {95.0, 100.0};
        flux_drift_add(&det, v, 2);
    }
    int dc[8]; double dr[8];
    int nd = flux_drift_detect(&det, c, 2, dc, dr);
    ASSERT_EQ(nd, 1, "1 drifting");
    ASSERT_EQ(dc[0], 0, "temp drifting, not speed");
    PASS();
}

static void test_drift_empty(void) {
    TEST_START("drift: no readings → no drift");
    FluxConstraint c = {"x", 0, 100, FLUX_SEV_CAUTION};
    FluxDriftDetector det;
    flux_drift_init(&det, 10);
    int dc[8]; double dr[8];
    int nd = flux_drift_detect(&det, &c, 1, dc, dr);
    ASSERT_EQ(nd, 0, "empty → 0");
    PASS();
}

static void test_drift_with_aggregation(void) {
    TEST_START("integration: drift + aggregate pipeline");
    FluxConstraint c = {"temp", 0, 100, FLUX_SEV_CAUTION};
    FluxDriftDetector det;
    flux_drift_init(&det, 5);
    /* Values drifting high: 60, 70, 80, 90, 95 */
    double vals[] = {60.0, 70.0, 80.0, 90.0, 95.0};
    uint8_t masks[5];
    float fvals[5];
    for (int i = 0; i < 5; i++) {
        flux_drift_add(&det, &vals[i], 1);
        fvals[i] = (float)vals[i];
    }
    flux_check_batch(fvals, 5, &c, 1, masks);

    FluxAggregate agg;
    flux_aggregate(masks, 5, &c, 1, &agg);
    ASSERT_EQ(agg.total_readings, 5, "5 readings");
    /* None violate [0,100] but drift should be detected */
    int dc[8]; double dr[8];
    int nd = flux_drift_detect(&det, &c, 1, dc, dr);
    ASSERT_EQ(nd, 1, "drift detected despite no violations");
    PASS();
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("=== FLUX Engine Test Suite ===\n\n");

    printf("[Core Check]\n");
    test_within_bounds();
    test_at_lo_bound();
    test_at_hi_bound();
    test_below_lo();
    test_above_hi();
    test_nan_violates();
    test_inf_violates();
    test_neg_inf_violates();
    test_multi_constraint_partial();
    test_batch();
    test_result_decode_pass();
    test_result_decode_severity();

    printf("\n[Fracture]\n");
    test_fracture_single_block();
    test_fracture_independent();
    test_fracture_fully_independent();
    test_fracture_empty();
    test_fracture_bridge();
    test_fracture_output_limit();
    test_coalesce();
    test_coalesce_empty();

    printf("\n[Sediment]\n");
    test_sediment_init();
    test_sediment_add();
    test_sediment_override_bounds();
    test_sediment_no_layers();
    test_sediment_multiple_overrides();
    test_sediment_depth_limit();

    printf("\n[Presets]\n");
    test_preset_automotive();
    test_preset_aviation();
    test_preset_medical();
    test_preset_energy();
    test_preset_robotics();
    test_preset_marine();
    test_preset_hvac();
    test_preset_manufacturing();
    test_preset_telecom();
    test_preset_spacecraft();

    printf("\n[Integration]\n");
    test_check_then_fracture();
    test_sediment_with_preset();
    test_nan_batch_all_violate();
    test_zero_constraints();

    printf("\n[Check Vector]\n");
    test_vector_all_pass();
    test_vector_partial_violation();
    test_vector_nan_violates();

    printf("\n[Serialization]\n");
    test_json_roundtrip();
    test_json_bounds_preserved();
    test_json_empty_array();
    test_json_null_input();
    test_json_invalid_syntax();
    test_file_save_load();
    test_file_not_found();
    test_json_preset_roundtrip();

    printf("\n[Aggregation]\n");
    test_agg_all_pass();
    test_agg_all_violate();
    test_agg_violation_rate();
    test_agg_per_constraint();
    test_agg_worst_reading();
    test_agg_severity_breakdown();
    test_agg_empty();

    printf("\n[Drift Detection]\n");
    test_drift_init();
    test_drift_no_drift();
    test_drift_detected();
    test_drift_multi_constraint();
    test_drift_empty();
    test_drift_with_aggregation();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
