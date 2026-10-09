/*
 * Ed25519, written from RFC 8032 section 5.1. See include\velocity9x\ed25519.h
 * for what uses it and the constant-time note.
 *
 * Three layers, bottom up:
 *
 *   Field arithmetic mod p = 2^255 - 19 on 32 limbs of radix 2^8. Each limb
 *   is a v9x_u32 so a limb product and a 32-term column of them fit with
 *   room to spare; the bounds that make that true are proved where they are
 *   relied on, in v9x_ed25519_fe_carry and v9x_ed25519_fe_mul.
 *
 *   Points on the twisted Edwards curve -x^2 + y^2 = 1 + d x^2 y^2 in
 *   extended coordinates (X:Y:Z:T), x = X/Z, y = Y/Z, x*y = T/Z, with the
 *   RFC 8032 5.1.4 addition and doubling formulas.
 *
 *   Scalars mod the group order L, as 32-byte little-endian arrays, reduced
 *   by binary long division - slow and obviously right.
 *
 * No C library function is called anywhere in this file: it links into
 * runtime-free tools, so copies and compares are explicit loops.
 */
#include "velocity9x/ed25519.h"
#include "velocity9x/sha512.h"

#define V9X_ED25519_LIMBS ((v9x_u16)32u)

/*
 * A field element: value = sum of limb[i] * 2^(8 i), i = 0..31.
 *
 * The representation spans 256 bits, one more than p needs, so it is not
 * unique: any value congruent mod p represents the same element. Every
 * function that returns a field element leaves it "carried" - limb[0] <=
 * 293 and limb[1..31] <= 255, see v9x_ed25519_fe_carry - and every function
 * that takes one relies on no more than that. Only
 * v9x_ed25519_fe_to_bytes produces the canonical value below p.
 */
struct v9x_ed25519_fe {
    v9x_u32 limb[32];
};

/* A point in extended homogeneous coordinates (RFC 8032 5.1.4). */
struct v9x_ed25519_point {
    struct v9x_ed25519_fe x;
    struct v9x_ed25519_fe y;
    struct v9x_ed25519_fe z;
    struct v9x_ed25519_fe t;
};

/*
 * Curve constants, little-endian, from RFC 8032 5.1.
 *
 * d = -121665/121666 mod p. sqrt(-1) = 2^((p-1)/4) mod p, the root RFC 8032
 * 5.1.3 step 3 multiplies by. B is the base point (x, 4/5) with x even, x
 * being the decimal RFC 8032 5.1 states. These were converted from the
 * RFC's definitions offline, and each is exercised by the section 7.1
 * vectors in tests\host\test_crypto.c: a wrong d breaks every addition, a
 * wrong B every public key, and a wrong sqrt(-1) the half of all point
 * decodings that take that branch.
 */
static const v9x_u8 v9x_ed25519_d_bytes[32] = {
    0xA3, 0x78, 0x59, 0x13, 0xCA, 0x4D, 0xEB, 0x75,
    0xAB, 0xD8, 0x41, 0x41, 0x4D, 0x0A, 0x70, 0x00,
    0x98, 0xE8, 0x79, 0x77, 0x79, 0x40, 0xC7, 0x8C,
    0x73, 0xFE, 0x6F, 0x2B, 0xEE, 0x6C, 0x03, 0x52
};

static const v9x_u8 v9x_ed25519_sqrtm1_bytes[32] = {
    0xB0, 0xA0, 0x0E, 0x4A, 0x27, 0x1B, 0xEE, 0xC4,
    0x78, 0xE4, 0x2F, 0xAD, 0x06, 0x18, 0x43, 0x2F,
    0xA7, 0xD7, 0xFB, 0x3D, 0x99, 0x00, 0x4D, 0x2B,
    0x0B, 0xDF, 0xC1, 0x4F, 0x80, 0x24, 0x83, 0x2B
};

static const v9x_u8 v9x_ed25519_base_x_bytes[32] = {
    0x1A, 0xD5, 0x25, 0x8F, 0x60, 0x2D, 0x56, 0xC9,
    0xB2, 0xA7, 0x25, 0x95, 0x60, 0xC7, 0x2C, 0x69,
    0x5C, 0xDC, 0xD6, 0xFD, 0x31, 0xE2, 0xA4, 0xC0,
    0xFE, 0x53, 0x6E, 0xCD, 0xD3, 0x36, 0x69, 0x21
};

static const v9x_u8 v9x_ed25519_base_y_bytes[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

/* p = 2^255 - 19, little-endian, for the final canonical reduction. */
static const v9x_u8 v9x_ed25519_p_bytes[32] = {
    0xED, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F
};

/* L = 2^252 + 27742317777372353535851937790883648493 (RFC 8032 5.1),
 * little-endian. */
static const v9x_u8 v9x_ed25519_l_bytes[32] = {
    0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
    0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/*
 * 8p as limbs, each large enough to subtract any carried limb from.
 *
 * 0x768 + sum(i = 1..30) 0x7F8 * 2^(8i) + 0x3F8 * 2^248 = 2^258 - 152 = 8p.
 * The smallest limb, 0x3F8 = 1016, exceeds the largest carried limb (293),
 * so a + 8p - b is non-negative limb by limb. Why 8p rather than p: p's own
 * limbs are mostly 0xFF, smaller than a carried limb may be.
 */
#define V9X_ED25519_8P_LOW  ((v9x_u32)0x768ul)
#define V9X_ED25519_8P_MID  ((v9x_u32)0x7F8ul)
#define V9X_ED25519_8P_HIGH ((v9x_u32)0x3F8ul)

/* 2^256 mod p: a carry out of the top limb is worth 2 * 19. */
#define V9X_ED25519_FOLD ((v9x_u32)38ul)

/* ------------------------------------------------------------------------ */
/* Byte helpers. */

static void v9x_ed25519_bytes_copy(v9x_u8 *out, const v9x_u8 *in, v9x_u16 n)
{
    v9x_u16 i;

    for (i = 0u; i < n; ++i) {
        out[i] = in[i];
    }
}

static v9x_u16 v9x_ed25519_bytes_equal(const v9x_u8 *a,
                                       const v9x_u8 *b,
                                       v9x_u16 n)
{
    v9x_u16 i;

    for (i = 0u; i < n; ++i) {
        if (a[i] != b[i]) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

/* a >= b, both 32-byte little-endian: compare from the top byte down. */
static v9x_u16 v9x_ed25519_bytes_ge(const v9x_u8 a[32], const v9x_u8 b[32])
{
    v9x_u16 i;

    for (i = 32u; i > 0u; --i) {
        if (a[i - 1u] != b[i - 1u]) {
            return (a[i - 1u] > b[i - 1u]) ? V9X_TRUE : V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

/* a -= b, both 32-byte little-endian, caller guarantees a >= b. */
static void v9x_ed25519_bytes_sub(v9x_u8 a[32], const v9x_u8 b[32])
{
    v9x_u32 borrow;
    v9x_u32 diff;
    v9x_u16 i;

    borrow = 0ul;
    for (i = 0u; i < 32u; ++i) {
        diff = (v9x_u32)a[i] + 0x100ul - (v9x_u32)b[i] - borrow;
        a[i] = (v9x_u8)(diff & 0xFFul);
        borrow = (diff >> 8) ? 0ul : 1ul;
    }
}

/* ------------------------------------------------------------------------ */
/* Field arithmetic mod p. */

static void v9x_ed25519_fe_copy(struct v9x_ed25519_fe *out,
                                const struct v9x_ed25519_fe *a)
{
    v9x_u16 i;

    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        out->limb[i] = a->limb[i];
    }
}

static void v9x_ed25519_fe_set_small(struct v9x_ed25519_fe *out, v9x_u32 n)
{
    v9x_u16 i;

    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        out->limb[i] = 0ul;
    }
    out->limb[0] = n;
}

/* Load 32 little-endian bytes as they are, all 256 bits. Byte limbs are
 * carried by definition. */
static void v9x_ed25519_fe_from_bytes(struct v9x_ed25519_fe *out,
                                      const v9x_u8 in[32])
{
    v9x_u16 i;

    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        out->limb[i] = (v9x_u32)in[i];
    }
}

/*
 * One carry pass: move everything above 8 bits of each limb into the next,
 * and fold what leaves the top limb back into limb 0 at 38 per unit, since
 * 2^256 = 2 * 2^255 = 2 * 19 = 38 (mod p). The value is unchanged mod p.
 */
static void v9x_ed25519_fe_carry_pass(struct v9x_ed25519_fe *a)
{
    v9x_u32 carry;
    v9x_u16 i;

    for (i = 0u; i < V9X_ED25519_LIMBS - 1u; ++i) {
        carry = a->limb[i] >> 8;
        a->limb[i] &= 0xFFul;
        a->limb[i + 1u] += carry;
    }
    carry = a->limb[31] >> 8;
    a->limb[31] &= 0xFFul;
    a->limb[0] += carry * V9X_ED25519_FOLD;
}

/*
 * Bring any limbs below 2^31 to the carried form.
 *
 * Pass 1. Each carry is at most (2^31 + 2^24) / 2^8 < 2^24, so no limb plus
 * an incoming carry reaches 2^32; the carry out of the top is under 2^24,
 * and 38 times it under 2^30, added to a limb 0 just masked to 8 bits.
 * Afterwards limb[1..31] <= 255 and limb[0] < 2^30 + 2^8, so the whole
 * value is below 2^256 + 2^30.
 *
 * Pass 2. The ripple makes limb[0..30] exact bytes, so limb[31] * 2^248 is
 * at most that value: limb[31] <= 256 and at most 1 leaves the top. limb[0]
 * is then at most 255 + 38 = 293, and every other limb at most 255.
 *
 * That is the carried form, and every multiplication bound below assumes
 * no more than it.
 */
static void v9x_ed25519_fe_carry(struct v9x_ed25519_fe *a)
{
    v9x_ed25519_fe_carry_pass(a);
    v9x_ed25519_fe_carry_pass(a);
}

/* out = a + b. Carried inputs give limbs <= 586 before the carry. Safe when
 * out aliases either input: each limb is read before it is written. */
static void v9x_ed25519_fe_add(struct v9x_ed25519_fe *out,
                               const struct v9x_ed25519_fe *a,
                               const struct v9x_ed25519_fe *b)
{
    v9x_u16 i;

    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        out->limb[i] = a->limb[i] + b->limb[i];
    }
    v9x_ed25519_fe_carry(out);
}

/* out = a - b, computed as a + 8p - b so that no limb goes negative; see
 * the 8P constants for why each limb difference is non-negative. Limbs stay
 * under 293 + 0x7F8 < 2^12 before the carry. Alias-safe like the add. */
static void v9x_ed25519_fe_sub(struct v9x_ed25519_fe *out,
                               const struct v9x_ed25519_fe *a,
                               const struct v9x_ed25519_fe *b)
{
    v9x_u16 i;
    v9x_u32 bias;

    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        if (i == 0u) {
            bias = V9X_ED25519_8P_LOW;
        } else if (i == V9X_ED25519_LIMBS - 1u) {
            bias = V9X_ED25519_8P_HIGH;
        } else {
            bias = V9X_ED25519_8P_MID;
        }
        out->limb[i] = a->limb[i] + bias - b->limb[i];
    }
    v9x_ed25519_fe_carry(out);
}

/*
 * out = a * b.
 *
 * Schoolbook product into 63 columns, column k = sum over i + j = k of
 * a[i] * b[j]. Carried limbs are below 2^9, so each product is below 2^18
 * and a column of at most 32 of them below 2^23.
 *
 * Column k + 32 carries weight 2^(8k) * 2^256 = 38 * 2^(8k) (mod p), so it
 * folds onto column k: column k + 38 * column (k + 32) < 39 * 2^23 < 2^29,
 * inside the 2^31 v9x_ed25519_fe_carry accepts.
 *
 * Computed into locals, so out may alias a or b.
 */
static void v9x_ed25519_fe_mul(struct v9x_ed25519_fe *out,
                               const struct v9x_ed25519_fe *a,
                               const struct v9x_ed25519_fe *b)
{
    v9x_u32 column[64];
    v9x_u16 i;
    v9x_u16 j;

    for (i = 0u; i < 64u; ++i) {
        column[i] = 0ul;
    }
    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        for (j = 0u; j < V9X_ED25519_LIMBS; ++j) {
            column[i + j] += a->limb[i] * b->limb[j];
        }
    }
    for (i = 0u; i < V9X_ED25519_LIMBS; ++i) {
        out->limb[i] = column[i] + V9X_ED25519_FOLD * column[i + 32u];
    }
    v9x_ed25519_fe_carry(out);
}

static void v9x_ed25519_fe_square(struct v9x_ed25519_fe *out,
                                  const struct v9x_ed25519_fe *a)
{
    v9x_ed25519_fe_mul(out, a, a);
}

/*
 * The canonical little-endian encoding, value reduced below p (RFC 8032
 * 5.1.2 encodes y this way).
 *
 * A third carry pass makes every limb a byte. From the carried form (limb[0]
 * <= 293, the rest <= 255) at most 1 ripples out of each limb. If 1 leaves
 * the top, every limb above 0 rippled to zero and limb 0 had been at least
 * 256, so it is now at most 37 + 38 = 75; if not, limb 0 is already a byte.
 *
 * The result is then below 2^256, which is below 3p, so subtracting p at
 * most twice, each time it still fits, lands below p.
 */
static void v9x_ed25519_fe_to_bytes(v9x_u8 out[32],
                                    const struct v9x_ed25519_fe *a)
{
    struct v9x_ed25519_fe t;
    v9x_u16 i;

    v9x_ed25519_fe_copy(&t, a);
    v9x_ed25519_fe_carry(&t);
    v9x_ed25519_fe_carry_pass(&t);
    for (i = 0u; i < 32u; ++i) {
        out[i] = (v9x_u8)t.limb[i];
    }
    for (i = 0u; i < 2u; ++i) {
        if (v9x_ed25519_bytes_ge(out, v9x_ed25519_p_bytes)) {
            v9x_ed25519_bytes_sub(out, v9x_ed25519_p_bytes);
        }
    }
}

static v9x_u16 v9x_ed25519_fe_equal(const struct v9x_ed25519_fe *a,
                                    const struct v9x_ed25519_fe *b)
{
    v9x_u8 a_bytes[32];
    v9x_u8 b_bytes[32];

    v9x_ed25519_fe_to_bytes(a_bytes, a);
    v9x_ed25519_fe_to_bytes(b_bytes, b);
    return v9x_ed25519_bytes_equal(a_bytes, b_bytes, 32u);
}

/* The low bit of the canonical value: RFC 8032's "x is negative". */
static v9x_u16 v9x_ed25519_fe_is_odd(const struct v9x_ed25519_fe *a)
{
    v9x_u8 bytes[32];

    v9x_ed25519_fe_to_bytes(bytes, a);
    return (v9x_u16)(bytes[0] & 1u);
}

static v9x_u16 v9x_ed25519_fe_is_zero(const struct v9x_ed25519_fe *a)
{
    struct v9x_ed25519_fe zero;

    v9x_ed25519_fe_set_small(&zero, 0ul);
    return v9x_ed25519_fe_equal(a, &zero);
}

/*
 * The exponents this file raises to are all of one shape: low byte, thirty
 * 0xFF bytes, high byte, little-endian.
 *
 *   p - 2       = 2^255 - 21: low 0xEB, high 0x7F (Fermat inversion)
 *   (p - 5) / 8 = 2^252 - 3:  low 0xFD, high 0x0F (RFC 8032 5.1.3 step 3)
 */
static void v9x_ed25519_exponent(v9x_u8 out[32], v9x_u8 low, v9x_u8 high)
{
    v9x_u16 i;

    out[0] = low;
    for (i = 1u; i < 31u; ++i) {
        out[i] = 0xFFu;
    }
    out[31] = high;
}

/* out = a^e by left-to-right square-and-multiply over all 256 bits of e.
 * Exponents here are public constants, so the branch leaks nothing. */
static void v9x_ed25519_fe_pow(struct v9x_ed25519_fe *out,
                               const struct v9x_ed25519_fe *a,
                               const v9x_u8 e[32])
{
    struct v9x_ed25519_fe base;
    struct v9x_ed25519_fe result;
    v9x_u16 bit;

    v9x_ed25519_fe_copy(&base, a);
    v9x_ed25519_fe_set_small(&result, 1ul);
    for (bit = 256u; bit > 0u; --bit) {
        v9x_ed25519_fe_square(&result, &result);
        if ((e[(bit - 1u) >> 3] >> ((bit - 1u) & 7u)) & 1u) {
            v9x_ed25519_fe_mul(&result, &result, &base);
        }
    }
    v9x_ed25519_fe_copy(out, &result);
}

/* 1/a = a^(p-2) (Fermat). Zero maps to zero, which no caller relies on. */
static void v9x_ed25519_fe_invert(struct v9x_ed25519_fe *out,
                                  const struct v9x_ed25519_fe *a)
{
    v9x_u8 e[32];

    v9x_ed25519_exponent(e, 0xEBu, 0x7Fu);
    v9x_ed25519_fe_pow(out, a, e);
}

/* ------------------------------------------------------------------------ */
/* Points. */

static void v9x_ed25519_point_copy(struct v9x_ed25519_point *out,
                                   const struct v9x_ed25519_point *a)
{
    v9x_ed25519_fe_copy(&out->x, &a->x);
    v9x_ed25519_fe_copy(&out->y, &a->y);
    v9x_ed25519_fe_copy(&out->z, &a->z);
    v9x_ed25519_fe_copy(&out->t, &a->t);
}

/* The neutral element (0, 1): X = 0, Y = Z = 1, T = 0. */
static void v9x_ed25519_point_identity(struct v9x_ed25519_point *out)
{
    v9x_ed25519_fe_set_small(&out->x, 0ul);
    v9x_ed25519_fe_set_small(&out->y, 1ul);
    v9x_ed25519_fe_set_small(&out->z, 1ul);
    v9x_ed25519_fe_set_small(&out->t, 0ul);
}

static void v9x_ed25519_point_base(struct v9x_ed25519_point *out)
{
    v9x_ed25519_fe_from_bytes(&out->x, v9x_ed25519_base_x_bytes);
    v9x_ed25519_fe_from_bytes(&out->y, v9x_ed25519_base_y_bytes);
    v9x_ed25519_fe_set_small(&out->z, 1ul);
    v9x_ed25519_fe_mul(&out->t, &out->x, &out->y);
}

/*
 * out = p + q, RFC 8032 5.1.4 (the a = -1 formulas):
 *
 *   A = (Y1-X1)*(Y2-X2), B = (Y1+X1)*(Y2+X2), C = T1*2*d*T2, D = Z1*2*Z2,
 *   E = B-A, F = D-C, G = D+C, H = B+A,
 *   X3 = E*F, Y3 = G*H, T3 = E*H, Z3 = F*G.
 *
 * Complete - correct for every pair of inputs, p == q included - and
 * computed entirely into locals, so out may alias either input.
 */
static void v9x_ed25519_point_add(struct v9x_ed25519_point *out,
                                  const struct v9x_ed25519_point *p,
                                  const struct v9x_ed25519_point *q)
{
    struct v9x_ed25519_fe d2;
    struct v9x_ed25519_fe a;
    struct v9x_ed25519_fe b;
    struct v9x_ed25519_fe c;
    struct v9x_ed25519_fe d;
    struct v9x_ed25519_fe e;
    struct v9x_ed25519_fe f;
    struct v9x_ed25519_fe g;
    struct v9x_ed25519_fe h;
    struct v9x_ed25519_fe t;

    v9x_ed25519_fe_from_bytes(&d2, v9x_ed25519_d_bytes);
    v9x_ed25519_fe_add(&d2, &d2, &d2);

    v9x_ed25519_fe_sub(&a, &p->y, &p->x);
    v9x_ed25519_fe_sub(&t, &q->y, &q->x);
    v9x_ed25519_fe_mul(&a, &a, &t);
    v9x_ed25519_fe_add(&b, &p->y, &p->x);
    v9x_ed25519_fe_add(&t, &q->y, &q->x);
    v9x_ed25519_fe_mul(&b, &b, &t);
    v9x_ed25519_fe_mul(&c, &p->t, &d2);
    v9x_ed25519_fe_mul(&c, &c, &q->t);
    v9x_ed25519_fe_add(&d, &p->z, &p->z);
    v9x_ed25519_fe_mul(&d, &d, &q->z);

    v9x_ed25519_fe_sub(&e, &b, &a);
    v9x_ed25519_fe_sub(&f, &d, &c);
    v9x_ed25519_fe_add(&g, &d, &c);
    v9x_ed25519_fe_add(&h, &b, &a);

    v9x_ed25519_fe_mul(&out->x, &e, &f);
    v9x_ed25519_fe_mul(&out->y, &g, &h);
    v9x_ed25519_fe_mul(&out->t, &e, &h);
    v9x_ed25519_fe_mul(&out->z, &f, &g);
}

/*
 * out = 2p, RFC 8032 5.1.4 doubling:
 *
 *   A = X1^2, B = Y1^2, C = 2*Z1^2, H = A+B, E = H-(X1+Y1)^2, G = A-B,
 *   F = C+G, X3 = E*F, Y3 = G*H, T3 = E*H, Z3 = F*G.
 *
 * Four multiplications fewer than the addition, and the scalar loop below
 * doubles on every bit. Alias-safe like the addition.
 */
static void v9x_ed25519_point_double(struct v9x_ed25519_point *out,
                                     const struct v9x_ed25519_point *p)
{
    struct v9x_ed25519_fe a;
    struct v9x_ed25519_fe b;
    struct v9x_ed25519_fe c;
    struct v9x_ed25519_fe e;
    struct v9x_ed25519_fe f;
    struct v9x_ed25519_fe g;
    struct v9x_ed25519_fe h;

    v9x_ed25519_fe_square(&a, &p->x);
    v9x_ed25519_fe_square(&b, &p->y);
    v9x_ed25519_fe_square(&c, &p->z);
    v9x_ed25519_fe_add(&c, &c, &c);
    v9x_ed25519_fe_add(&h, &a, &b);
    v9x_ed25519_fe_add(&e, &p->x, &p->y);
    v9x_ed25519_fe_square(&e, &e);
    v9x_ed25519_fe_sub(&e, &h, &e);
    v9x_ed25519_fe_sub(&g, &a, &b);
    v9x_ed25519_fe_add(&f, &c, &g);

    v9x_ed25519_fe_mul(&out->x, &e, &f);
    v9x_ed25519_fe_mul(&out->y, &g, &h);
    v9x_ed25519_fe_mul(&out->t, &e, &h);
    v9x_ed25519_fe_mul(&out->z, &f, &g);
}

/*
 * out = [scalar]p, double-and-add from the top bit of a 32-byte
 * little-endian scalar.
 *
 * Not constant-time: whether an addition happens follows the scalar's bits.
 * In verification the scalars (S and k) are public. In signing they are
 * secret (s and r), and that is accepted only because signing runs offline
 * on the developer's PC - see include\velocity9x\ed25519.h.
 */
static void v9x_ed25519_point_scalar_mul(struct v9x_ed25519_point *out,
                                         const struct v9x_ed25519_point *p,
                                         const v9x_u8 scalar[32])
{
    struct v9x_ed25519_point base;
    struct v9x_ed25519_point result;
    v9x_u16 bit;

    v9x_ed25519_point_copy(&base, p);
    v9x_ed25519_point_identity(&result);
    for (bit = 256u; bit > 0u; --bit) {
        v9x_ed25519_point_double(&result, &result);
        if ((scalar[(bit - 1u) >> 3] >> ((bit - 1u) & 7u)) & 1u) {
            v9x_ed25519_point_add(&result, &result, &base);
        }
    }
    v9x_ed25519_point_copy(out, &result);
}

/* RFC 8032 5.1.2: y canonical and little-endian, with the low bit of x in
 * the top bit of the last byte (free, because y < p < 2^255). */
static void v9x_ed25519_point_encode(v9x_u8 out[32],
                                     const struct v9x_ed25519_point *p)
{
    struct v9x_ed25519_fe z_inverse;
    struct v9x_ed25519_fe x;
    struct v9x_ed25519_fe y;

    v9x_ed25519_fe_invert(&z_inverse, &p->z);
    v9x_ed25519_fe_mul(&x, &p->x, &z_inverse);
    v9x_ed25519_fe_mul(&y, &p->y, &z_inverse);
    v9x_ed25519_fe_to_bytes(out, &y);
    if (v9x_ed25519_fe_is_odd(&x)) {
        out[31] = (v9x_u8)(out[31] | 0x80u);
    }
}

/*
 * RFC 8032 5.1.3: decode a point, or refuse.
 *
 * Step 1: the top bit is x_0, the rest is y, and y >= p is a failure - so
 * every accepted encoding is the unique canonical one, which is what lets
 * verification compare points by their encodings.
 *
 * Steps 2 and 3: x^2 = (y^2 - 1) / (d y^2 + 1) = u/v, and the candidate root
 * is x = u v^3 (u v^7)^((p-5)/8), which needs no inversion. If v x^2 == u, x
 * is a root; if v x^2 == -u, x * sqrt(-1) is; otherwise u/v is not a square
 * and the encoding is not a point.
 *
 * Step 4: x == 0 with x_0 == 1 is refused (there is no "negative zero");
 * otherwise x is replaced by p - x when its parity is not x_0.
 */
static v9x_u16 v9x_ed25519_point_decode(struct v9x_ed25519_point *out,
                                        const v9x_u8 in[32])
{
    v9x_u8 y_bytes[32];
    v9x_u8 canonical[32];
    v9x_u8 e[32];
    v9x_u16 x_0;
    struct v9x_ed25519_fe one;
    struct v9x_ed25519_fe d;
    struct v9x_ed25519_fe y;
    struct v9x_ed25519_fe y2;
    struct v9x_ed25519_fe u;
    struct v9x_ed25519_fe v;
    struct v9x_ed25519_fe v3;
    struct v9x_ed25519_fe t;
    struct v9x_ed25519_fe x;
    struct v9x_ed25519_fe vx2;
    struct v9x_ed25519_fe neg_u;

    /* Step 1. Reducing y and comparing with what was given detects y >= p:
     * a value already below p encodes as itself and any other does not. */
    v9x_ed25519_bytes_copy(y_bytes, in, 32u);
    x_0 = (v9x_u16)((y_bytes[31] >> 7) & 1u);
    y_bytes[31] = (v9x_u8)(y_bytes[31] & 0x7Fu);
    v9x_ed25519_fe_from_bytes(&y, y_bytes);
    v9x_ed25519_fe_to_bytes(canonical, &y);
    if (!v9x_ed25519_bytes_equal(canonical, y_bytes, 32u)) {
        return V9X_FALSE;
    }

    /* Step 2: u = y^2 - 1, v = d y^2 + 1. */
    v9x_ed25519_fe_set_small(&one, 1ul);
    v9x_ed25519_fe_from_bytes(&d, v9x_ed25519_d_bytes);
    v9x_ed25519_fe_square(&y2, &y);
    v9x_ed25519_fe_sub(&u, &y2, &one);
    v9x_ed25519_fe_mul(&v, &d, &y2);
    v9x_ed25519_fe_add(&v, &v, &one);

    /* x = u v^3 (u v^7)^((p-5)/8). */
    v9x_ed25519_fe_square(&v3, &v);
    v9x_ed25519_fe_mul(&v3, &v3, &v);
    v9x_ed25519_fe_square(&t, &v3);
    v9x_ed25519_fe_mul(&t, &t, &v);
    v9x_ed25519_fe_mul(&t, &t, &u);
    v9x_ed25519_exponent(e, 0xFDu, 0x0Fu);
    v9x_ed25519_fe_pow(&t, &t, e);
    v9x_ed25519_fe_mul(&x, &u, &v3);
    v9x_ed25519_fe_mul(&x, &x, &t);

    /* Step 3. */
    v9x_ed25519_fe_square(&vx2, &x);
    v9x_ed25519_fe_mul(&vx2, &vx2, &v);
    if (!v9x_ed25519_fe_equal(&vx2, &u)) {
        v9x_ed25519_fe_set_small(&neg_u, 0ul);
        v9x_ed25519_fe_sub(&neg_u, &neg_u, &u);
        if (!v9x_ed25519_fe_equal(&vx2, &neg_u)) {
            return V9X_FALSE;
        }
        v9x_ed25519_fe_from_bytes(&t, v9x_ed25519_sqrtm1_bytes);
        v9x_ed25519_fe_mul(&x, &x, &t);
    }

    /* Step 4. */
    if (v9x_ed25519_fe_is_zero(&x) && x_0 == 1u) {
        return V9X_FALSE;
    }
    if (v9x_ed25519_fe_is_odd(&x) != x_0) {
        v9x_ed25519_fe_set_small(&t, 0ul);
        v9x_ed25519_fe_sub(&x, &t, &x);
    }

    v9x_ed25519_fe_copy(&out->x, &x);
    v9x_ed25519_fe_copy(&out->y, &y);
    v9x_ed25519_fe_set_small(&out->z, 1ul);
    v9x_ed25519_fe_mul(&out->t, &x, &y);
    return V9X_TRUE;
}

/* ------------------------------------------------------------------------ */
/* Scalars mod L. */

/*
 * out = in mod L for a 64-byte little-endian in (a SHA-512 digest, or a
 * product), by binary long division: shift the remainder left one bit at a
 * time, bringing in the next bit of in from the top, and subtract L whenever
 * the remainder reaches it.
 *
 * The remainder is below L < 2^253 before each shift and below 2L < 2^254
 * after, so 32 bytes always hold it and one subtraction always suffices.
 * 512 steps of 32-byte arithmetic: slow, and nowhere near the field cost.
 */
static void v9x_ed25519_scalar_reduce(v9x_u8 out[32], const v9x_u8 in[64])
{
    v9x_u8 remainder[32];
    v9x_u16 bit;
    v9x_u16 i;
    v9x_u16 incoming;
    v9x_u16 outgoing;

    for (i = 0u; i < 32u; ++i) {
        remainder[i] = 0u;
    }
    for (bit = 512u; bit > 0u; --bit) {
        incoming = (v9x_u16)((in[(bit - 1u) >> 3] >> ((bit - 1u) & 7u)) & 1u);
        for (i = 0u; i < 32u; ++i) {
            outgoing = (v9x_u16)((remainder[i] >> 7) & 1u);
            remainder[i] = (v9x_u8)(((remainder[i] << 1) | incoming) & 0xFFu);
            incoming = outgoing;
        }
        if (v9x_ed25519_bytes_ge(remainder, v9x_ed25519_l_bytes)) {
            v9x_ed25519_bytes_sub(remainder, v9x_ed25519_l_bytes);
        }
    }
    v9x_ed25519_bytes_copy(out, remainder, 32u);
}

/*
 * out = (a * b + c) mod L, all 32-byte little-endian (RFC 8032 5.1.6 step
 * 5, S = r + k * s).
 *
 * Byte-by-byte schoolbook product: a column holds at most 32 products below
 * 2^16 plus one byte of c, under 2^22, and the carry rippled into it is
 * below 2^14, so v9x_u32 never overflows. a * b + c < 2^512, so 64 bytes
 * hold the whole sum before the reduction.
 */
static void v9x_ed25519_scalar_muladd(v9x_u8 out[32],
                                      const v9x_u8 a[32],
                                      const v9x_u8 b[32],
                                      const v9x_u8 c[32])
{
    v9x_u32 column[64];
    v9x_u8 wide[64];
    v9x_u32 carry;
    v9x_u16 i;
    v9x_u16 j;

    for (i = 0u; i < 64u; ++i) {
        column[i] = 0ul;
    }
    for (i = 0u; i < 32u; ++i) {
        for (j = 0u; j < 32u; ++j) {
            column[i + j] += (v9x_u32)a[i] * (v9x_u32)b[j];
        }
        column[i] += (v9x_u32)c[i];
    }
    carry = 0ul;
    for (i = 0u; i < 64u; ++i) {
        carry += column[i];
        wide[i] = (v9x_u8)(carry & 0xFFul);
        carry >>= 8;
    }
    v9x_ed25519_scalar_reduce(out, wide);
}

/* ------------------------------------------------------------------------ */
/* RFC 8032 5.1.5 - 5.1.7. */

/* RFC 8032 5.1.5 steps 1 and 2: hash the seed, prune the low half into the
 * secret scalar s (clear the 3 low bits and the top bit, set bit 254). */
static void v9x_ed25519_expand_seed(const v9x_u8 seed[32],
                                    v9x_u8 scalar[32],
                                    v9x_u8 prefix[32])
{
    struct v9x_sha512 sha;
    v9x_u8 digest[64];

    v9x_sha512_init(&sha);
    v9x_sha512_update(&sha, seed, 32ul);
    v9x_sha512_final(&sha, digest);
    v9x_ed25519_bytes_copy(scalar, digest, 32u);
    v9x_ed25519_bytes_copy(prefix, digest + 32, 32u);
    scalar[0] = (v9x_u8)(scalar[0] & 0xF8u);
    scalar[31] = (v9x_u8)((scalar[31] & 0x7Fu) | 0x40u);
}

/* k = SHA-512(R || A || M) mod L (RFC 8032 5.1.6 step 4, 5.1.7 step 2). */
static void v9x_ed25519_challenge(v9x_u8 k[32],
                                  const v9x_u8 r_bytes[32],
                                  const v9x_u8 public_key[32],
                                  const v9x_u8 *message,
                                  v9x_u32 length)
{
    struct v9x_sha512 sha;
    v9x_u8 digest[64];

    v9x_sha512_init(&sha);
    v9x_sha512_update(&sha, r_bytes, 32ul);
    v9x_sha512_update(&sha, public_key, 32ul);
    v9x_sha512_update(&sha, message, length);
    v9x_sha512_final(&sha, digest);
    v9x_ed25519_scalar_reduce(k, digest);
}

void v9x_ed25519_public_key(const v9x_u8 seed[32], v9x_u8 public_key[32])
{
    v9x_u8 scalar[32];
    v9x_u8 prefix[32];
    struct v9x_ed25519_point base;
    struct v9x_ed25519_point a;

    /* RFC 8032 5.1.5 steps 3 and 4: A = [s]B, encoded. */
    v9x_ed25519_expand_seed(seed, scalar, prefix);
    v9x_ed25519_point_base(&base);
    v9x_ed25519_point_scalar_mul(&a, &base, scalar);
    v9x_ed25519_point_encode(public_key, &a);
}

void v9x_ed25519_sign(const v9x_u8 seed[32],
                      const v9x_u8 public_key[32],
                      const v9x_u8 *message,
                      v9x_u32 length,
                      v9x_u8 signature[64])
{
    struct v9x_sha512 sha;
    struct v9x_ed25519_point base;
    struct v9x_ed25519_point r_point;
    v9x_u8 scalar[32];
    v9x_u8 prefix[32];
    v9x_u8 digest[64];
    v9x_u8 r[32];
    v9x_u8 k[32];

    /* Step 1. */
    v9x_ed25519_expand_seed(seed, scalar, prefix);

    /* Step 2: r = SHA-512(prefix || M) mod L. */
    v9x_sha512_init(&sha);
    v9x_sha512_update(&sha, prefix, 32ul);
    v9x_sha512_update(&sha, message, length);
    v9x_sha512_final(&sha, digest);
    v9x_ed25519_scalar_reduce(r, digest);

    /* Step 3: R = [r]B, encoded into the first half of the signature. */
    v9x_ed25519_point_base(&base);
    v9x_ed25519_point_scalar_mul(&r_point, &base, r);
    v9x_ed25519_point_encode(signature, &r_point);

    /* Steps 4 and 5: S = (r + k * s) mod L into the second half. */
    v9x_ed25519_challenge(k, signature, public_key, message, length);
    v9x_ed25519_scalar_muladd(signature + 32, k, scalar, r);
}

v9x_u16 v9x_ed25519_verify(const v9x_u8 signature[64],
                           const v9x_u8 public_key[32],
                           const v9x_u8 *message,
                           v9x_u32 length)
{
    struct v9x_ed25519_point base;
    struct v9x_ed25519_point a;
    struct v9x_ed25519_point r_point;
    struct v9x_ed25519_point lhs;
    struct v9x_ed25519_point rhs;
    v9x_u8 k[32];
    v9x_u8 lhs_bytes[32];
    v9x_u8 rhs_bytes[32];

    /* Step 1. S must be below L: without this check S + L would verify as
     * well as S, and a signature would not be unique. Checked first because
     * it is the cheapest refusal. */
    if (v9x_ed25519_bytes_ge(signature + 32, v9x_ed25519_l_bytes)) {
        return V9X_FALSE;
    }
    if (!v9x_ed25519_point_decode(&a, public_key)) {
        return V9X_FALSE;
    }
    if (!v9x_ed25519_point_decode(&r_point, signature)) {
        return V9X_FALSE;
    }

    /* Step 2. k is hashed over the bytes as given; strict decoding above
     * means they are the canonical encodings of A and R. */
    v9x_ed25519_challenge(k, signature, public_key, message, length);

    /* Step 3, cofactorless: [S]B == R + [k]A, compared as encodings. Both
     * are canonical, so equal encodings are equal points. */
    v9x_ed25519_point_base(&base);
    v9x_ed25519_point_scalar_mul(&lhs, &base, signature + 32);
    v9x_ed25519_point_scalar_mul(&rhs, &a, k);
    v9x_ed25519_point_add(&rhs, &r_point, &rhs);
    v9x_ed25519_point_encode(lhs_bytes, &lhs);
    v9x_ed25519_point_encode(rhs_bytes, &rhs);
    return v9x_ed25519_bytes_equal(lhs_bytes, rhs_bytes, 32u);
}
