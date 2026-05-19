/**
 * bench_flux_engine.c — Throughput benchmark
 *
 * Compile: gcc -O2 -lm -o bench_flux_engine bench_flux_engine.c
 */

#define _POSIX_C_SOURCE 199309L

#define FLUX_ENGINE_IMPLEMENTATION
#include "flux_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void bench_single_check(const FluxConstraint *c, int n, int iters) {
    double t0 = now_sec();
    volatile uint8_t sink = 0;
    for (int i = 0; i < iters; i++) {
        float val = (float)((i % 2000) - 1000);
        sink |= flux_check(val, c, n);
    }
    double elapsed = now_sec() - t0;
    printf("  Single check: %.1fM checks/sec (%d iters, %.3fms)\n",
           iters / elapsed / 1e6, iters, elapsed * 1000);
    (void)sink;
}

static void bench_batch_check(const FluxConstraint *c, int n, int count) {
    float *vals = (float *)malloc((size_t)count * sizeof(float));
    uint8_t *masks = (uint8_t *)malloc((size_t)count);
    for (int i = 0; i < count; i++)
        vals[i] = (float)((i % 2000) - 1000);

    double t0 = now_sec();
    flux_check_batch(vals, count, c, n, masks);
    double elapsed = now_sec() - t0;
    printf("  Batch (%dK):   %.1fM checks/sec (%.3fms)\n",
           count / 1000, count / elapsed / 1e6, elapsed * 1000);

    free(vals);
    free(masks);
}

static void bench_sediment(const FluxConstraint *base, int n, int count) {
    FluxSedimentStack stack;
    flux_sediment_init(&stack);
    flux_sediment_add(&stack, 0, 10.0f, 90.0f);
    flux_sediment_add(&stack, 1, 5.0f, 50.0f);

    float *vals = (float *)malloc((size_t)count * sizeof(float));
    uint8_t *masks = (uint8_t *)malloc((size_t)count);
    for (int i = 0; i < count; i++)
        vals[i] = (float)((i % 200) - 100);

    double t0 = now_sec();
    flux_sediment_apply(&stack, vals, count, base, n, masks);
    double elapsed = now_sec() - t0;
    printf("  Sediment (%dK): %.1fM checks/sec (%.3fms)\n",
           count / 1000, count / elapsed / 1e6, elapsed * 1000);

    free(vals);
    free(masks);
}

static void bench_fracture(int nc, int nd) {
    /* Build a graph where every constraint touches 2 dims */
    FluxGraph g;
    flux_graph_init(&g, nc, nd);
    for (int i = 0; i < nc; i++) {
        g.adj[i * nd + (i % nd)] = 1;
        g.adj[i * nd + ((i + 1) % nd)] = 1;
    }

    FluxBlock blocks[FLUX_MAX_BLOCKS];

    double t0 = now_sec();
    int nb = flux_fracture(&g, blocks, FLUX_MAX_BLOCKS);
    double elapsed = now_sec() - t0;

    printf("  Fracture (%dC×%dD): %d blocks in %.3fms\n", nc, nd, nb, elapsed * 1000);

    flux_graph_free(&g);
    for (int i = 0; i < nb; i++) { free(blocks[i].constraint_indices); free(blocks[i].dim_indices); }
}

int main(void) {
    printf("=== FLUX Engine Benchmark ===\n\n");

    FluxConstraint auto_c[8];
    int n = flux_preset_automotive(auto_c);

    printf("Automotive preset (8 constraints):\n");
    bench_single_check(auto_c, n, 10000000);
    bench_batch_check(auto_c, n, 1000000);
    bench_sediment(auto_c, n, 1000000);

    printf("\nFracture benchmarks:\n");
    bench_fracture(8, 8);
    bench_fracture(16, 16);
    bench_fracture(32, 32);

    printf("\nMedical preset (8 constraints):\n");
    FluxConstraint med_c[8];
    flux_preset_medical(med_c);
    bench_single_check(med_c, 8, 10000000);
    bench_batch_check(med_c, 8, 1000000);

    printf("\nDone.\n");
    return 0;
}
