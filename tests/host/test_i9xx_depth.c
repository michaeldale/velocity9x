/*
 * The Gen3 depth comparison table, pinned.
 *
 * Seven of the eight D3DCMP values happen to map to their own number and
 * the eighth does not, which is exactly the shape of table that gets
 * "simplified" into arithmetic by someone who checks three entries. All
 * eight are asserted here, along with the refusal, because an off-by-one in
 * a depth comparison does not crash - it renders the wrong picture, on a
 * machine reached by carrying a USB stick to it.
 */
#include <stdio.h>

#include "velocity9x/i9xx_depth.h"

static unsigned int depth_failures = 0u;

#define DEPTHCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++depth_failures; \
    } \
} while (0)

/* Every arm, against Mesa 20.3.5's i915_reg.h and i915_state_inlines.h. */
static void test_every_function(void)
{
    v9x_u32 encoded = 0xfffffffful;

    DEPTHCHECK(v9x_i9xx_depth_func(1ul, &encoded) == V9X_TRUE);   /* NEVER */
    DEPTHCHECK(encoded == 1ul);
    DEPTHCHECK(v9x_i9xx_depth_func(2ul, &encoded) == V9X_TRUE);   /* LESS */
    DEPTHCHECK(encoded == 2ul);
    DEPTHCHECK(v9x_i9xx_depth_func(3ul, &encoded) == V9X_TRUE);   /* EQUAL */
    DEPTHCHECK(encoded == 3ul);
    DEPTHCHECK(v9x_i9xx_depth_func(4ul, &encoded) == V9X_TRUE);   /* LEQUAL */
    DEPTHCHECK(encoded == 4ul);
    DEPTHCHECK(v9x_i9xx_depth_func(5ul, &encoded) == V9X_TRUE);   /* GREATER */
    DEPTHCHECK(encoded == 5ul);
    DEPTHCHECK(v9x_i9xx_depth_func(6ul, &encoded) == V9X_TRUE);   /* NOTEQUAL */
    DEPTHCHECK(encoded == 6ul);
    DEPTHCHECK(v9x_i9xx_depth_func(7ul, &encoded) == V9X_TRUE);   /* GEQUAL */
    DEPTHCHECK(encoded == 7ul);
}

/*
 * The one that is not its own number. D3DCMP_ALWAYS is 8 and the S6 field's
 * ALWAYS is 0, so a table written as "subtract nothing" would emit 8 into a
 * three-bit field and set an unrelated bit.
 */
static void test_always_is_zero_not_eight(void)
{
    v9x_u32 encoded = 0xfffffffful;

    DEPTHCHECK(v9x_i9xx_depth_func(8ul, &encoded) == V9X_TRUE);
    DEPTHCHECK(encoded == 0ul);
    /* And it fits the field it is written into. */
    DEPTHCHECK(encoded <= 7ul);
}

/*
 * A value outside the enumeration is refused and the output left alone.
 * Falling back to ALWAYS would draw every fragment, which is a worse
 * picture than declining the depth test and is what Mesa's own default arm
 * does - it can, because its input is a validated enumeration and a HAL's
 * is whatever an application put in a render state.
 */
static void test_an_unknown_function_is_refused(void)
{
    v9x_u32 encoded = 0x5a5a5a5aul;

    DEPTHCHECK(v9x_i9xx_depth_func(0ul, &encoded) == V9X_FALSE);
    DEPTHCHECK(encoded == 0x5a5a5a5aul);
    DEPTHCHECK(v9x_i9xx_depth_func(9ul, &encoded) == V9X_FALSE);
    DEPTHCHECK(encoded == 0x5a5a5a5aul);
    DEPTHCHECK(v9x_i9xx_depth_func(0xfffffffful, &encoded) == V9X_FALSE);
    DEPTHCHECK(encoded == 0x5a5a5a5aul);
    DEPTHCHECK(v9x_i9xx_depth_func(4ul, 0) == V9X_FALSE);
}

/*
 * The function intel98 measured 3DMark99 asking for, and the one the engine
 * used to refuse: 192,069 of 224,838 draws lost their depth test to it.
 */
static void test_the_function_3dmark99_asks_for(void)
{
    v9x_u32 encoded = 0ul;

    DEPTHCHECK(v9x_i9xx_depth_func(4ul, &encoded) == V9X_TRUE);
    DEPTHCHECK(encoded == V9X_I9XX_COMPAREFUNC_LEQUAL_V);
    /* And it is NOT the value the engine emitted for everything. */
    DEPTHCHECK(encoded != V9X_I9XX_COMPAREFUNC_LESS_V);
}

/*
 * The alpha test's field: enable at 31, function at 30:28, reference at
 * 27:20 (intel_gen3_3d.h, and the Phase 6 scene intel47 measured).
 */
static void test_alpha_test_bits(void)
{
    v9x_u32 bits = 0x5a5a5a5aul;

    /* Off, whatever the other two say. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(0ul, 5ul, 0x80ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0ul);

    /* What Half-Life asks for: GREATER and NOTEQUAL against zero. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 5ul, 0ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0xd0000000ul);
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 6ul, 0ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0xe0000000ul);

    /* The Phase 6 scene's own test, GREATER against 0x80, rebuilt from
     * Direct3D terms: the same dword the scene measured. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 5ul, 0x80ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0xd8000000ul);

    /* ALWAYS is no test, and emitted as none. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 8ul, 0x80ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0ul);

    /* NEVER is a test, and honoured. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 1ul, 0ul, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0x90000000ul);

    /* The reference: a byte as a byte, the top of the byte range, and
     * DirectX 5's 16.16 above it - one half, one, and past one. */
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 7ul, 0xfful, &bits) == V9X_TRUE);
    DEPTHCHECK(bits == 0xfff00000ul);
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 7ul, 0x8000ul, &bits) ==
               V9X_TRUE);
    DEPTHCHECK(bits == (0xf0000000ul | (0x80ul << 20)));
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 7ul, 0x10000ul, &bits) ==
               V9X_TRUE);
    DEPTHCHECK(bits == 0xfff00000ul);
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 7ul, 0xfffffffful, &bits) ==
               V9X_TRUE);
    DEPTHCHECK(bits == 0xfff00000ul);

    /* An unknown function is refused and the output left alone. */
    bits = 0x5a5a5a5aul;
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 0ul, 0ul, &bits) == V9X_FALSE);
    DEPTHCHECK(bits == 0x5a5a5a5aul);
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 9ul, 0ul, &bits) == V9X_FALSE);
    DEPTHCHECK(bits == 0x5a5a5a5aul);
    DEPTHCHECK(v9x_i9xx_alpha_test_bits(1ul, 5ul, 0ul, 0) == V9X_FALSE);
}

unsigned int v9x_run_i9xx_depth_tests(void)
{
    depth_failures = 0u;
    test_every_function();
    test_always_is_zero_not_eight();
    test_an_unknown_function_is_refused();
    test_the_function_3dmark99_asks_for();
    test_alpha_test_bits();

    return depth_failures;
}
