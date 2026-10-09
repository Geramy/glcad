// The GPU mirror of gcm's exact predicates (docs/08 §4).
//
// The same orientation tests as `gcm::Predicates`, computed on a GPU, and
// required to give the SAME SIGN for the same lattice input -- not a similar
// answer, the same one. A kernel that agrees with the CPU on most inputs is a
// kernel that produces a mesh with a hole in it on the rest, and finding out
// which is why the conformance suite beside this file exists.
//
// Three things shape the implementation, and each is a place a naive port
// silently diverges:
//
//   * 32-BIT LIMBS. Not every target has 64-bit integers, and the ones that do
//     do not all have them at full speed. Everything here is built from uint,
//     which is the one width every GPU has, so the mirror is the same code on
//     every device rather than one version per capability.
//
//   * NO 64-BIT MULTIPLY. The exact tier needs the full 128-bit product of two
//     64-bit differences, and a GPU has no instruction for it. It is built
//     from four 32x32->64 partial products, carried by hand.
//
//   * FLOATING POINT IS NOT THE ANSWER, IT IS THE FILTER. The fast path is
//     exactly Shewchuk's, with exactly the same bound, and it decides only
//     when the error term proves it may. A GPU whose float differs from the
//     CPU's by one ulp still lands in the same branch, because the branch is
//     taken on a proven margin and not on the value.
module predicates;

// --- 64-bit integers from 32-bit limbs --------------------------------------
// Two limbs, little end first, two's complement. A struct rather than uint2 so
// the arithmetic below cannot be mistaken for a vector operation.
struct I64 {
    uint lo;
    uint hi;
};

I64 i64_from_int(int v) {
    I64 r;
    r.lo = uint(v);
    // Sign extension by hand: the shift of a negative int is
    // implementation-defined in more languages than not, and a wrong sign here
    // makes every predicate wrong for half the plane.
    r.hi = (v < 0) ? 0xFFFFFFFFu : 0u;
    return r;
}

I64 i64_from_parts(uint lo, uint hi) {
    I64 r;
    r.lo = lo;
    r.hi = hi;
    return r;
}

bool i64_negative(I64 a) { return (a.hi & 0x80000000u) != 0u; }

I64 i64_add(I64 a, I64 b) {
    I64 r;
    r.lo = a.lo + b.lo;
    // The carry, without a carry flag: the sum wrapped exactly when it came
    // out smaller than an addend.
    uint carry = (r.lo < a.lo) ? 1u : 0u;
    r.hi = a.hi + b.hi + carry;
    return r;
}

I64 i64_neg(I64 a) {
    I64 r;
    r.lo = ~a.lo;
    r.hi = ~a.hi;
    I64 one;
    one.lo = 1u;
    one.hi = 0u;
    return i64_add(r, one);
}

I64 i64_sub(I64 a, I64 b) { return i64_add(a, i64_neg(b)); }

// --- 128-bit accumulators ---------------------------------------------------
// Four limbs. orient2d needs 128 bits for the difference of two products of
// 64-bit differences; orient3d needs 256, built from these.
struct I128 {
    uint l0;
    uint l1;
    uint l2;
    uint l3;
};

I128 i128_zero() {
    I128 r;
    r.l0 = 0u; r.l1 = 0u; r.l2 = 0u; r.l3 = 0u;
    return r;
}

bool i128_negative(I128 a) { return (a.l3 & 0x80000000u) != 0u; }

bool i128_is_zero(I128 a) {
    return (a.l0 | a.l1 | a.l2 | a.l3) == 0u;
}

I128 i128_add(I128 a, I128 b) {
    I128 r;
    uint carry = 0u;
    uint s;

    s = a.l0 + b.l0;            carry = (s < a.l0) ? 1u : 0u; r.l0 = s;
    s = a.l1 + b.l1;            uint c1 = (s < a.l1) ? 1u : 0u;
    uint t = s + carry;         c1 += (t < s) ? 1u : 0u;      r.l1 = t; carry = c1;
    s = a.l2 + b.l2;            uint c2 = (s < a.l2) ? 1u : 0u;
    t = s + carry;              c2 += (t < s) ? 1u : 0u;      r.l2 = t; carry = c2;
    // The top limb wraps and no carry escapes: 128 bits is enough for every
    // product this file forms, and a carry out of it would mean the caller
    // handed in coordinates outside the lattice.
    r.l3 = a.l3 + b.l3 + carry;
    return r;
}

I128 i128_neg(I128 a) {
    I128 r;
    r.l0 = ~a.l0; r.l1 = ~a.l1; r.l2 = ~a.l2; r.l3 = ~a.l3;
    I128 one = i128_zero();
    one.l0 = 1u;
    return i128_add(r, one);
}

I128 i128_sub(I128 a, I128 b) { return i128_add(a, i128_neg(b)); }

// The full 64-bit product of two 32-bit values, as a hi/lo pair. A GPU has no
// 64x64 multiply, so this is the primitive everything else is built from.
void mul32(uint a, uint b, out uint lo, out uint hi) {
    uint a_lo = a & 0xFFFFu, a_hi = a >> 16;
    uint b_lo = b & 0xFFFFu, b_hi = b >> 16;

    uint ll = a_lo * b_lo;
    uint lh = a_lo * b_hi;
    uint hl = a_hi * b_lo;
    uint hh = a_hi * b_hi;

    // The two cross terms are 32 bits each and are added at bit 16, so their
    // sum can carry into bit 32. Adding them before shifting is what loses
    // that carry, and the loss shows up only for large operands -- which is
    // exactly what a lattice coordinate is.
    uint mid = lh + hl;
    uint mid_carry = (mid < lh) ? 0x10000u : 0u;

    uint low = ll + (mid << 16);
    uint low_carry = (low < ll) ? 1u : 0u;

    lo = low;
    hi = hh + (mid >> 16) + mid_carry + low_carry;
}

// The 128-bit product of two 64-bit signed values. Computed on the magnitudes
// and signed at the end: a two's-complement multiply of the limbs directly
// needs a wider intermediate than the target has.
I128 mul_i64(I64 a, I64 b) {
    bool negative = i64_negative(a) != i64_negative(b);
    I64 ua = i64_negative(a) ? i64_neg(a) : a;
    I64 ub = i64_negative(b) ? i64_neg(b) : b;

    uint p00_lo, p00_hi, p01_lo, p01_hi, p10_lo, p10_hi, p11_lo, p11_hi;
    mul32(ua.lo, ub.lo, p00_lo, p00_hi);
    mul32(ua.lo, ub.hi, p01_lo, p01_hi);
    mul32(ua.hi, ub.lo, p10_lo, p10_hi);
    mul32(ua.hi, ub.hi, p11_lo, p11_hi);

    I128 r = i128_zero();
    r.l0 = p00_lo;

    // Limb 1: the high half of the low product plus the two cross terms.
    uint sum = p00_hi;
    uint carry = 0u;
    uint t = sum + p01_lo; carry += (t < sum) ? 1u : 0u; sum = t;
    t = sum + p10_lo;      carry += (t < sum) ? 1u : 0u; sum = t;
    r.l1 = sum;

    // Limb 2: the cross terms' high halves, the top product's low half, and
    // whatever carried out of limb 1.
    sum = p11_lo;
    uint carry2 = 0u;
    t = sum + p01_hi; carry2 += (t < sum) ? 1u : 0u; sum = t;
    t = sum + p10_hi; carry2 += (t < sum) ? 1u : 0u; sum = t;
    t = sum + carry;  carry2 += (t < sum) ? 1u : 0u; sum = t;
    r.l2 = sum;

    r.l3 = p11_hi + carry2;

    return negative ? i128_neg(r) : r;
}

// The product of a 128-bit value and a 64-bit one, kept to 128 bits. Used by
// orient3d, where the intermediate minors are already 128 bits wide.
I128 mul_i128_i64(I128 a, I64 b) {
    bool negative = i128_negative(a) != i64_negative(b);
    I128 ua = i128_negative(a) ? i128_neg(a) : a;
    I64  ub = i64_negative(b) ? i64_neg(b) : b;

    I128 acc = i128_zero();

    // Schoolbook over the limbs of b, shifting a as it goes. Only the terms
    // that land inside 128 bits are formed; the rest cannot contribute to a
    // determinant that fits.
    uint blimb[2];
    blimb[0] = ub.lo;
    blimb[1] = ub.hi;

    uint alimb[4];
    alimb[0] = ua.l0; alimb[1] = ua.l1; alimb[2] = ua.l2; alimb[3] = ua.l3;

    for (int j = 0; j < 2; ++j) {
        if (blimb[j] == 0u) continue;
        for (int i = 0; i + j < 4; ++i) {
            if (alimb[i] == 0u) continue;
            uint lo, hi;
            mul32(alimb[i], blimb[j], lo, hi);

            I128 term = i128_zero();
            int at = i + j;
            if (at == 0)      { term.l0 = lo; term.l1 = hi; }
            else if (at == 1) { term.l1 = lo; term.l2 = hi; }
            else if (at == 2) { term.l2 = lo; term.l3 = hi; }
            else              { term.l3 = lo; }
            acc = i128_add(acc, term);
        }
    }
    return negative ? i128_neg(acc) : acc;
}

// --- the sign ---------------------------------------------------------------
// -1, 0 or +1. The SAME three answers the CPU gives, because a caller that
// branches on them has to branch the same way on both.
int i128_sign(I128 a) {
    if (i128_is_zero(a)) return 0;
    return i128_negative(a) ? -1 : 1;
}

// --- the predicates ---------------------------------------------------------
// Shewchuk's bounds, spelled out rather than derived, so a reader can check
// them against the C++ beside them. eps is 2^-53 for binary64.
static const double kFilterEps = 1.1102230246251565e-16;
static const double kCcwBound  = (3.0 + 16.0 * kFilterEps) * kFilterEps;
static const double kO3dBound  = (7.0 + 56.0 * kFilterEps) * kFilterEps;

int orient2d_exact(I64 ax, I64 ay, I64 bx, I64 by, I64 cx, I64 cy) {
    I64 adx = i64_sub(ax, cx), ady = i64_sub(ay, cy);
    I64 bdx = i64_sub(bx, cx), bdy = i64_sub(by, cy);
    return i128_sign(i128_sub(mul_i64(adx, bdy), mul_i64(ady, bdx)));
}

// The filtered form. Identical in structure to the C++, including the bound,
// so the two take the same branch on the same input.
int orient2d(I64 ax, I64 ay, I64 bx, I64 by, I64 cx, I64 cy,
             double dax, double day, double dbx, double dby,
             double dcx, double dcy) {
    double adx = dax - dcx, ady = day - dcy;
    double bdx = dbx - dcx, bdy = dby - dcy;

    double left = adx * bdy;
    double right = ady * bdx;
    double det = left - right;
    double err = kCcwBound * (abs(left) + abs(right));

    if (det > err) return 1;
    if (-det > err) return -1;
    // Not proven either way: the exact tier decides, and it is the same
    // arithmetic the CPU runs.
    return orient2d_exact(ax, ay, bx, by, cx, cy);
}

int orient3d_exact(I64 ax, I64 ay, I64 az, I64 bx, I64 by, I64 bz,
                   I64 cx, I64 cy, I64 cz, I64 dx, I64 dy, I64 dz) {
    I64 adx = i64_sub(ax, dx), ady = i64_sub(ay, dy), adz = i64_sub(az, dz);
    I64 bdx = i64_sub(bx, dx), bdy = i64_sub(by, dy), bdz = i64_sub(bz, dz);
    I64 cdx = i64_sub(cx, dx), cdy = i64_sub(cy, dy), cdz = i64_sub(cz, dz);

    I128 m1 = i128_sub(mul_i64(bdy, cdz), mul_i64(bdz, cdy));
    I128 m2 = i128_sub(mul_i64(bdx, cdz), mul_i64(bdz, cdx));
    I128 m3 = i128_sub(mul_i64(bdx, cdy), mul_i64(bdy, cdx));

    I128 det = mul_i128_i64(m1, adx);
    det = i128_sub(det, mul_i128_i64(m2, ady));
    det = i128_add(det, mul_i128_i64(m3, adz));
    return i128_sign(det);
}

// --- the entry points -------------------------------------------------------
// Buffers of lattice coordinates in, signs out. One thread per test, which is
// the shape every caller in the product wants: a batch of predicates over a
// mesh, not one at a time.
struct Orient2dInput {
    int ax, ay, bx, by, cx, cy;
};

// A pair of 64-bit values, as limbs. The predicates take lattice coordinates,
// which the exact tier then differences -- so a test that only goes through
// them can never present the multiply with an arbitrary 64-bit operand. This
// entry point does, because the multiply is where the hard part is.
struct MulInput {
    uint a_lo, a_hi;
    uint b_lo, b_hi;
};

struct MulOutput {
    uint l0, l1, l2, l3;
};

struct Orient3dInput {
    int ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz;
};

StructuredBuffer<Orient2dInput>  g_in2;
RWStructuredBuffer<int>          g_out2;
StructuredBuffer<Orient3dInput>  g_in3;
RWStructuredBuffer<int>          g_out3;
StructuredBuffer<MulInput>       g_in_mul;
RWStructuredBuffer<MulOutput>    g_out_mul;

[shader("compute")]
[numthreads(64, 1, 1)]
void orient2d_batch(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    Orient2dInput v = g_in2[i];
    g_out2[i] = orient2d(
        i64_from_int(v.ax), i64_from_int(v.ay),
        i64_from_int(v.bx), i64_from_int(v.by),
        i64_from_int(v.cx), i64_from_int(v.cy),
        double(v.ax), double(v.ay), double(v.bx),
        double(v.by), double(v.cx), double(v.cy));
}

// The 128-bit product on its own. Checked limb for limb against the CPU's,
// because a carry lost inside it is worth far less than 2^96 and does not
// change the sign of a determinant built from large coordinates -- so a suite
// that only tested the predicates would report agreement while the arithmetic
// was wrong.
[shader("compute")]
[numthreads(64, 1, 1)]
void mul_batch(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    MulInput v = g_in_mul[i];
    I128 p = mul_i64(i64_from_parts(v.a_lo, v.a_hi),
                     i64_from_parts(v.b_lo, v.b_hi));
    MulOutput r;
    r.l0 = p.l0;
    r.l1 = p.l1;
    r.l2 = p.l2;
    r.l3 = p.l3;
    g_out_mul[i] = r;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void orient3d_batch(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    Orient3dInput v = g_in3[i];
    g_out3[i] = orient3d_exact(
        i64_from_int(v.ax), i64_from_int(v.ay), i64_from_int(v.az),
        i64_from_int(v.bx), i64_from_int(v.by), i64_from_int(v.bz),
        i64_from_int(v.cx), i64_from_int(v.cy), i64_from_int(v.cz),
        i64_from_int(v.dx), i64_from_int(v.dy), i64_from_int(v.dz));
}
