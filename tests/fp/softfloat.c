/*
 * IEEE 754 double precision arithmetic in software (round to nearest even)
 * Algorithms after John R. Hauser's SoftFloat 2.
 *
 * The compiler keeps all floating point values as double on the stack and
 * calls these functions with pointers. float is only a storage format.
 */

#ifdef __CC9__
typedef unsigned long u32;
typedef long          s32;
#define SF_API
#else
/* host test build */
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t  s32;
#define SF_API
#endif

typedef struct { u32 h, l; } U64;     /* h = most significant word */
typedef struct { u32 hi, lo; } dbl;   /* memory layout of a double (big endian) */

/* ---------------- 64 bit helpers ---------------- */

static void u64_shl(U64 *a, int n)
{
   if (n <= 0) return;
   if (n >= 64) { a->h = 0; a->l = 0; return; }
   if (n >= 32) { a->h = a->l << (n - 32); a->l = 0; return; }
   a->h = (a->h << n) | (a->l >> (32 - n));
   a->l <<= n;
}

/* shift right, set lsb if any 1 bits are shifted out */
static void u64_shr_jam(U64 *a, int n)
{
   u32 sticky;
   if (n <= 0) return;
   if (n >= 64)
   {
      sticky = (a->h | a->l) != 0;
      a->h = 0;
      a->l = sticky;
      return;
   }
   if (n >= 32)
   {
      sticky = a->l != 0;
      if (n > 32) sticky |= (a->h << (64 - n)) != 0;
      a->l = n == 32 ? a->h : a->h >> (n - 32);
      a->h = 0;
      a->l |= sticky;
      return;
   }
   sticky = (a->l << (32 - n)) != 0;
   a->l = (a->l >> n) | (a->h << (32 - n)) | sticky;
   a->h >>= n;
}

static void u64_add(U64 *r, U64 *a, U64 *b)
{
   u32 l = a->l + b->l;
   r->h = a->h + b->h + (l < a->l);
   r->l = l;
}

static void u64_sub(U64 *r, U64 *a, U64 *b)
{
   u32 borrow = a->l < b->l;
   r->l = a->l - b->l;
   r->h = a->h - b->h - borrow;
}

static int u64_lt(U64 *a, U64 *b)
{
   return a->h < b->h || (a->h == b->h && a->l < b->l);
}

static int clz32(u32 a)
{
   int n = 0;
   if (a == 0) return 32;
   if (!(a & 0xFFFF0000UL)) { n += 16; a <<= 16; }
   if (!(a & 0xFF000000UL)) { n += 8;  a <<= 8; }
   if (!(a & 0xF0000000UL)) { n += 4;  a <<= 4; }
   if (!(a & 0xC0000000UL)) { n += 2;  a <<= 2; }
   if (!(a & 0x80000000UL)) { n += 1; }
   return n;
}

static int clz64(U64 *a)
{
   if (a->h) return clz32(a->h);
   return 32 + clz32(a->l);
}

/* ---------------- pack / unpack ---------------- */

static void unpack(dbl *a, int *sign, int *exp, U64 *sig)
{
   *sign = (int)(a->hi >> 31);
   *exp = (int)((a->hi >> 20) & 0x7FF);
   sig->h = a->hi & 0x000FFFFFUL;
   sig->l = a->lo;
}

/* (sign << 63) + (exp << 52) + sig   (addition: carry into the exponent) */
static void pack(dbl *r, int sign, int exp, U64 *sig)
{
   u32 hi = ((u32)sign << 31) + ((u32)exp << 20) + sig->h;
   r->hi = hi;
   r->lo = sig->l;
}

static void set_nan(dbl *r)
{
   r->hi = 0x7FF80000UL;
   r->lo = 0;
}

static void set_inf(dbl *r, int sign)
{
   r->hi = ((u32)sign << 31) | 0x7FF00000UL;
   r->lo = 0;
}

static void set_zero(dbl *r, int sign)
{
   r->hi = (u32)sign << 31;
   r->lo = 0;
}

static int is_nan(dbl *a)
{
   return ((a->hi >> 20) & 0x7FF) == 0x7FF && ((a->hi & 0x000FFFFFUL) | a->lo);
}

/* zSig: leading bit at bit 62, zExp = biased exponent - 1 */
static void round_pack(dbl *r, int sign, int exp, U64 *sig)
{
   unsigned round_bits = (unsigned)(sig->l & 0x3FF);
   U64 t;
   if (exp >= 0x7FD || exp < 0)
   {
      if (exp > 0x7FD)
      {
         set_inf(r, sign);
         return;
      }
      if (exp == 0x7FD)
      {
         t.h = 0; t.l = 0x200;
         u64_add(&t, sig, &t);
         if (t.h & 0x80000000UL) { set_inf(r, sign); return; }
      }
      if (exp < 0)
      {
         u64_shr_jam(sig, -exp);
         exp = 0;
         round_bits = (unsigned)(sig->l & 0x3FF);
      }
   }
   t.h = 0; t.l = 0x200;
   u64_add(sig, sig, &t);
   /* >> 10 */
   sig->l = (sig->l >> 10) | (sig->h << 22);
   sig->h >>= 10;
   if (round_bits == 0x200) sig->l &= ~1UL;   /* ties to even */
   if ((sig->h | sig->l) == 0) exp = 0;
   pack(r, sign, exp, sig);
}

static void norm_round_pack(dbl *r, int sign, int exp, U64 *sig)
{
   int shift = clz64(sig) - 1;
   u64_shl(sig, shift);
   round_pack(r, sign, exp - shift, sig);
}

static void norm_subnormal(U64 *sig, int *exp)
{
   int shift = clz64(sig) - 11;
   u64_shl(sig, shift);
   *exp = 1 - shift;
}

/* ---------------- add / sub ---------------- */

static void add_sigs(dbl *r, dbl *a, dbl *b, int zsign)
{
   int as, bs, ae, be, ze, diff;
   U64 asig, bsig, zsig;
   unpack(a, &as, &ae, &asig);
   unpack(b, &bs, &be, &bsig);
   diff = ae - be;
   u64_shl(&asig, 9);
   u64_shl(&bsig, 9);
   if (diff > 0)
   {
      if (ae == 0x7FF) { if (asig.h | asig.l) set_nan(r); else *r = *a; return; }
      if (be == 0) --diff; else bsig.h |= 0x20000000UL;
      u64_shr_jam(&bsig, diff);
      ze = ae;
   }
   else if (diff < 0)
   {
      if (be == 0x7FF)
      {
         if (bsig.h | bsig.l) set_nan(r); else set_inf(r, zsign);
         return;
      }
      if (ae == 0) ++diff; else asig.h |= 0x20000000UL;
      u64_shr_jam(&asig, -diff);
      ze = be;
   }
   else
   {
      if (ae == 0x7FF)
      {
         if (asig.h | asig.l | bsig.h | bsig.l) set_nan(r); else *r = *a;
         return;
      }
      if (ae == 0)
      {
         u64_add(&zsig, &asig, &bsig);
         zsig.l = (zsig.l >> 9) | (zsig.h << 23);
         zsig.h >>= 9;
         pack(r, zsign, 0, &zsig);
         return;
      }
      u64_add(&zsig, &asig, &bsig);
      zsig.h += 0x40000000UL;
      round_pack(r, zsign, ae, &zsig);
      return;
   }
   asig.h |= 0x20000000UL;
   u64_add(&zsig, &asig, &bsig);
   --ze;
   if (!(zsig.h & 0x40000000UL))
   {
      /* no carry: shift left once more */
      u64_shl(&zsig, 1);
   }
   else ++ze;
   round_pack(r, zsign, ze, &zsig);
}

static void sub_sigs(dbl *r, dbl *a, dbl *b, int zsign)
{
   int as, bs, ae, be, ze, diff;
   U64 asig, bsig, zsig;
   unpack(a, &as, &ae, &asig);
   unpack(b, &bs, &be, &bsig);
   diff = ae - be;
   u64_shl(&asig, 10);
   u64_shl(&bsig, 10);
   if (diff > 0) goto a_exp_bigger;
   if (diff < 0) goto b_exp_bigger;
   if (ae == 0x7FF) { set_nan(r); return; }
   if (ae == 0) { ae = 1; be = 1; }
   if (u64_lt(&bsig, &asig)) goto a_bigger;
   if (u64_lt(&asig, &bsig)) goto b_bigger;
   set_zero(r, 0);
   return;
b_exp_bigger:
   if (be == 0x7FF)
   {
      if (bsig.h | bsig.l) set_nan(r); else set_inf(r, zsign ^ 1);
      return;
   }
   if (ae == 0) ++diff; else asig.h |= 0x40000000UL;
   u64_shr_jam(&asig, -diff);
   bsig.h |= 0x40000000UL;
b_bigger:
   u64_sub(&zsig, &bsig, &asig);
   ze = be;
   zsign ^= 1;
   goto norm;
a_exp_bigger:
   if (ae == 0x7FF) { if (asig.h | asig.l) set_nan(r); else *r = *a; return; }
   if (be == 0) --diff; else bsig.h |= 0x40000000UL;
   u64_shr_jam(&bsig, diff);
   asig.h |= 0x40000000UL;
a_bigger:
   u64_sub(&zsig, &asig, &bsig);
   ze = ae;
norm:
   --ze;
   norm_round_pack(r, zsign, ze, &zsig);
}

SF_API void __dadd(dbl *r, dbl *a, dbl *b)
{
   int as = (int)(a->hi >> 31), bs = (int)(b->hi >> 31);
   dbl x, y;
   x = *a; y = *b;
   if (as == bs) add_sigs(r, &x, &y, as);
   else sub_sigs(r, &x, &y, as);
}

SF_API void __dsub(dbl *r, dbl *a, dbl *b)
{
   int as = (int)(a->hi >> 31), bs = (int)(b->hi >> 31);
   dbl x, y;
   x = *a; y = *b;
   if (as == bs) sub_sigs(r, &x, &y, as);
   else add_sigs(r, &x, &y, as);
}

/* ---------------- mul ---------------- */

static void mul64(U64 *a, U64 *b, U64 *hi, U64 *lo)
{
   unsigned x[4], y[4];
   u32 res[8], t, carry;
   int i, j;
   x[0] = (unsigned)(a->l & 0xFFFF); x[1] = (unsigned)(a->l >> 16);
   x[2] = (unsigned)(a->h & 0xFFFF); x[3] = (unsigned)(a->h >> 16);
   y[0] = (unsigned)(b->l & 0xFFFF); y[1] = (unsigned)(b->l >> 16);
   y[2] = (unsigned)(b->h & 0xFFFF); y[3] = (unsigned)(b->h >> 16);
   for (i = 0; i < 8; i++) res[i] = 0;
   for (i = 0; i < 4; i++)
   {
      carry = 0;
      if (x[i] == 0) continue;
      for (j = 0; j < 4; j++)
      {
         t = (u32)x[i] * y[j] + res[i + j] + carry;
         res[i + j] = t & 0xFFFF;
         carry = t >> 16;
      }
      res[i + 4] = carry;
   }
   lo->l = res[0] | (res[1] << 16);
   lo->h = res[2] | (res[3] << 16);
   hi->l = res[4] | (res[5] << 16);
   hi->h = res[6] | (res[7] << 16);
}

SF_API void __dmul(dbl *r, dbl *a, dbl *b)
{
   int as, bs, ae, be, zs, ze;
   U64 asig, bsig, z0, z1;
   unpack(a, &as, &ae, &asig);
   unpack(b, &bs, &be, &bsig);
   zs = as ^ bs;
   if (ae == 0x7FF)
   {
      if ((asig.h | asig.l) || (be == 0x7FF && (bsig.h | bsig.l))) { set_nan(r); return; }
      if (be == 0 && (bsig.h | bsig.l) == 0) { set_nan(r); return; }
      set_inf(r, zs);
      return;
   }
   if (be == 0x7FF)
   {
      if (bsig.h | bsig.l) { set_nan(r); return; }
      if (ae == 0 && (asig.h | asig.l) == 0) { set_nan(r); return; }
      set_inf(r, zs);
      return;
   }
   if (ae == 0)
   {
      if ((asig.h | asig.l) == 0) { set_zero(r, zs); return; }
      norm_subnormal(&asig, &ae);
   }
   if (be == 0)
   {
      if ((bsig.h | bsig.l) == 0) { set_zero(r, zs); return; }
      norm_subnormal(&bsig, &be);
   }
   ze = ae + be - 0x3FF;
   asig.h |= 0x00100000UL;
   bsig.h |= 0x00100000UL;
   u64_shl(&asig, 10);
   u64_shl(&bsig, 11);
   mul64(&asig, &bsig, &z0, &z1);
   if (z1.h | z1.l) z0.l |= 1;
   if (!(z0.h & 0x40000000UL))
   {
      u64_shl(&z0, 1);
      --ze;
   }
   round_pack(r, zs, ze, &z0);
}

/* ---------------- div ---------------- */

SF_API void __ddiv(dbl *r, dbl *a, dbl *b)
{
   int as, bs, ae, be, zs, ze, i;
   U64 asig, bsig, q;
   unpack(a, &as, &ae, &asig);
   unpack(b, &bs, &be, &bsig);
   zs = as ^ bs;
   if (ae == 0x7FF)
   {
      if (asig.h | asig.l) { set_nan(r); return; }
      if (be == 0x7FF) { set_nan(r); return; }
      set_inf(r, zs);
      return;
   }
   if (be == 0x7FF)
   {
      if (bsig.h | bsig.l) { set_nan(r); return; }
      set_zero(r, zs);
      return;
   }
   if (be == 0)
   {
      if ((bsig.h | bsig.l) == 0)
      {
         if (ae == 0 && (asig.h | asig.l) == 0) set_nan(r);
         else set_inf(r, zs);
         return;
      }
      norm_subnormal(&bsig, &be);
   }
   if (ae == 0)
   {
      if ((asig.h | asig.l) == 0) { set_zero(r, zs); return; }
      norm_subnormal(&asig, &ae);
   }
   asig.h |= 0x00100000UL;
   bsig.h |= 0x00100000UL;
   ze = ae - be + 0x3FE;
   if (u64_lt(&asig, &bsig))
   {
      u64_shl(&asig, 1);
      --ze;
   }
   /* restoring division: 63 quotient bits */
   q.h = 0; q.l = 0;
   for (i = 0; i < 63; i++)
   {
      /* q <<= 1 */
      q.h = (q.h << 1) | (q.l >> 31);
      q.l <<= 1;
      if (!u64_lt(&asig, &bsig))
      {
         u64_sub(&asig, &asig, &bsig);
         q.l |= 1;
      }
      u64_shl(&asig, 1);
   }
   if (asig.h | asig.l) q.l |= 1;
   round_pack(r, zs, ze, &q);
}

/* ---------------- compare ---------------- */

/* -1: a < b, 0: equal, 1: a > b, 2: unordered */
SF_API int __dcmp(dbl *a, dbl *b)
{
   int as, bs;
   if (is_nan(a) || is_nan(b)) return 2;
   as = (int)(a->hi >> 31);
   bs = (int)(b->hi >> 31);
   if (((a->hi | b->hi) & 0x7FFFFFFFUL) == 0 && (a->lo | b->lo) == 0) return 0;
   if (as != bs) return as ? -1 : 1;
   if (a->hi == b->hi && a->lo == b->lo) return 0;
   {
      int less = a->hi < b->hi || (a->hi == b->hi && a->lo < b->lo);
      if (as) less = !less;
      return less ? -1 : 1;
   }
}

/* ---------------- conversions ---------------- */

static void from_u32(dbl *r, int sign, u32 v)
{
   U64 sig;
   int shift;
   if (v == 0) { set_zero(r, 0); return; }
   shift = clz32(v) + 21;      /* leading bit to bit 52 */
   sig.h = 0;
   sig.l = v;
   u64_shl(&sig, shift);
   sig.h &= 0x000FFFFFUL;
   pack(r, sign, 0x3FF + 52 - shift + 0 + 0, &sig);
   /* exponent: value = 1.f * 2^(52-shift+32-32) */
}

SF_API void __l2d(dbl *r, s32 v)
{
   if (v < 0) from_u32(r, 1, (u32)0 - (u32)v);
   else from_u32(r, 0, (u32)v);
}

SF_API void __ul2d(dbl *r, u32 v)
{
   from_u32(r, 0, v);
}

/* truncate towards zero; returns magnitude, *ok = 0 on overflow */
static u32 to_u32(dbl *a, int *sign, int *ovf)
{
   int s, e, shift;
   U64 sig;
   unpack(a, &s, &e, &sig);
   *sign = s;
   *ovf = 0;
   if (e == 0x7FF) { *ovf = 1; return 0; }
   if (e < 0x3FF) return 0;
   shift = e - 0x3FF;           /* value = 1.f * 2^shift */
   if (shift > 31) { *ovf = 1; return 0; }
   sig.h |= 0x00100000UL;
   /* integer part = sig >> (52 - shift) */
   if (52 - shift >= 32) return sig.h >> (52 - shift - 32);
   return (sig.l >> (52 - shift)) | (sig.h << (shift - 20));
}

SF_API s32 __d2l(dbl *a)
{
   int s, ovf;
   u32 m = to_u32(a, &s, &ovf);
   if (ovf || (!s && m > 0x7FFFFFFFUL) || (s && m > 0x80000000UL))
      return s ? (s32)0x80000000UL : 0x7FFFFFFFL;
   return s ? (s32)((u32)0 - m) : (s32)m;
}

SF_API u32 __d2ul(dbl *a)
{
   int s, ovf;
   u32 m = to_u32(a, &s, &ovf);
   if (ovf) return s ? 0 : 0xFFFFFFFFUL;
   if (s) return (u32)0 - m;
   return m;
}

SF_API void __f2d(dbl *r, u32 f)
{
   int s = (int)(f >> 31);
   int e = (int)((f >> 23) & 0xFF);
   u32 frac = f & 0x007FFFFFUL;
   U64 sig;
   if (e == 0xFF)
   {
      if (frac) { r->hi = ((u32)s << 31) | 0x7FF80000UL | (frac >> 3); r->lo = frac << 29; }
      else set_inf(r, s);
      return;
   }
   if (e == 0)
   {
      if (frac == 0) { set_zero(r, s); return; }
      /* subnormal float: normalize */
      e = 1;
      while (!(frac & 0x00800000UL)) { frac <<= 1; e--; }
      frac &= 0x007FFFFFUL;
   }
   sig.h = frac >> 3;
   sig.l = frac << 29;
   pack(r, s, e - 127 + 1023, &sig);
}

SF_API u32 __d2f(dbl *a)
{
   int s, e;
   U64 sig;
   u32 z, round_bits;
   unpack(a, &s, &e, &sig);
   if (e == 0x7FF)
   {
      if (sig.h | sig.l) return ((u32)s << 31) | 0x7FC00000UL | (sig.h << 3);
      return ((u32)s << 31) | 0x7F800000UL;
   }
   u64_shr_jam(&sig, 22);
   z = sig.l;
   if (e || z)
   {
      z |= 0x40000000UL;
      e -= 0x381;
   }
   round_bits = z & 0x7F;
   if (e >= 0xFD || e < 0)
   {
      if (e > 0xFD || (e == 0xFD && ((z + 0x40) & 0x80000000UL)))
         return ((u32)s << 31) | 0x7F800000UL;
      if (e < 0)
      {
         int n = -e;
         if (n >= 32) z = z != 0;
         else z = (z >> n) | ((z << (32 - n)) != 0);
         e = 0;
         round_bits = z & 0x7F;
      }
   }
   z = (z + 0x40) >> 7;
   if (round_bits == 0x40) z &= ~1UL;
   if (z == 0) e = 0;
   return ((u32)s << 31) + ((u32)e << 23) + z;
}
