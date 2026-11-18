import struct, random, math, sys
random.seed(int(sys.argv[1]) if len(sys.argv)>1 else 1)
N=int(sys.argv[2]) if len(sys.argv)>2 else 60
def bits(x): return struct.unpack('>Q',struct.pack('>d',x))[0]
def fbits(x): return struct.unpack('>I',struct.pack('>f',x))[0]
def dfrom(b): return struct.unpack('>d',struct.pack('>Q',b))[0]
def rnd(mode):
    if mode==0: return dfrom(random.getrandbits(64))
    if mode==1:
        e=1023+random.randint(-40,40); return dfrom((random.getrandbits(1)<<63)|(e<<52)|random.getrandbits(52))
    if mode==2:
        e=random.choice([random.randint(0,60),random.randint(1980,2046)]); return dfrom((random.getrandbits(1)<<63)|(e<<52)|random.getrandbits(52))
    sp=[0.0,-0.0,1.0,-1.0,0.5,3.0,1e308,-1e308,5e-324,2.2250738585072014e-308,math.inf,-math.inf,1.5,0.1,1e-300,123456789.0,2.0**31,-2.0**31,4294967295.0]
    return random.choice(sp)
import numpy as np
def f32(x):
    with np.errstate(all='ignore'): return float(np.float32(x))
rows=[]
for i in range(N):
    m=i%4
    a=rnd(m); b=rnd(m)
    if m==1 and i%3==0: b=a*(1+random.choice([1,-1])*2.0**-random.randint(1,60))
    with np.errstate(all='ignore'):
        A=np.float64(a); B=np.float64(b)
        r=[float(A+B),float(A-B),float(A*B),float(A/B) if b!=0 or True else 0]
    def nb(x): 
        return 0x7FF8000000000000 if math.isnan(x) else bits(x)
    c = 2 if (math.isnan(a) or math.isnan(b)) else (-1 if a<b else (1 if a>b else 0))
    fa=f32(a); fb = 0x7FC00000 if math.isnan(fa) else fbits(fa)
    t=math.trunc(a) if math.isfinite(a) else 0
    lv = t if (math.isfinite(a) and -2**31<=t<2**31) else None
    wb = 0x7FF8000000000000 if math.isnan(fa) else bits(fa)
    ul = random.getrandbits(32); sl = random.getrandbits(32) - 2**31
    rows.append((nb(a),nb(b),[nb(x) for x in r],c,fb,lv,wb,ul,bits(float(ul)),sl,bits(float(sl))))
print('#include <stdio.h>')
print('typedef struct { unsigned long h, l; } Q;')
print('typedef union { double d; Q q; } U;')
print('static const Q in[][2] = {')
for r in rows: print('{{0x%08xUL,0x%08xUL},{0x%08xUL,0x%08xUL}},'%(r[0]>>32,r[0]&0xffffffff,r[1]>>32,r[1]&0xffffffff))
print('};')
print('static const Q out[][4] = {')
for r in rows: print('{'+','.join('{0x%08xUL,0x%08xUL}'%(x>>32,x&0xffffffff) for x in r[2])+'},')
print('};')
print('static const int cmpres[] = {'+','.join(str(r[3]) for r in rows)+'};')
print('static const unsigned long fres[] = {'+','.join('0x%08xUL'%r[4] for r in rows)+'};')
print('static const long lres[] = {'+','.join(('%dL'%r[5]) if r[5] is not None else '0x7fffffffL' for r in rows)+'};')
print('static const char lok[] = {'+','.join('1' if r[5] is not None else '0' for r in rows)+'};')
print('static const Q wres[] = {'+','.join('{0x%08xUL,0x%08xUL}'%(r[6]>>32,r[6]&0xffffffff) for r in rows)+'};')
print('static const unsigned long uin[] = {'+','.join('0x%08xUL'%r[7] for r in rows)+'};')
print('static const Q ures[] = {'+','.join('{0x%08xUL,0x%08xUL}'%(r[8]>>32,r[8]&0xffffffff) for r in rows)+'};')
print('static const long sin_[] = {'+','.join('%dL'%r[9] if r[9]!=-2**31 else '(-2147483647L-1)' for r in rows)+'};')
print('static const Q sres[] = {'+','.join('{0x%08xUL,0x%08xUL}'%(r[10]>>32,r[10]&0xffffffff) for r in rows)+'};')
print('''
int bad;
static void chk(int i, const char *op, U r, const Q *e)
{
   if (r.q.h != e->h || r.q.l != e->l)
   {
      if ((e->h & 0x7FF00000UL) == 0x7FF00000UL && (e->h & 0xFFFFF || e->l) &&
          (r.q.h & 0x7FF00000UL) == 0x7FF00000UL && (r.q.h & 0xFFFFF || r.q.l)) return;
      bad++;
      if (bad < 30) printf("%d %s: %08lx%08lx got %08lx%08lx\\n", i, op, e->h, e->l, r.q.h, r.q.l);
   }
}
int main(void)
{
   int i, n = sizeof(in) / sizeof(in[0]);
   for (i = 0; i < n; i++)
   {
      U a, b, r;
      float f;
      union { float f; unsigned long u; } fu;
      a.q = in[i][0];
      b.q = in[i][1];
      r.d = a.d + b.d; chk(i, "add", r, &out[i][0]);
      r.d = a.d - b.d; chk(i, "sub", r, &out[i][1]);
      r.d = a.d * b.d; chk(i, "mul", r, &out[i][2]);
      r.d = a.d / b.d; chk(i, "div", r, &out[i][3]);
      {
         int c = a.d < b.d ? -1 : a.d > b.d ? 1 : a.d == b.d ? 0 : 2;
         if (c != cmpres[i]) { bad++; if (bad < 30) printf("%d cmp %d got %d\\n", i, cmpres[i], c); }
      }
      fu.f = (float)a.d;
      if (fu.u != fres[i] && !((fres[i] & 0x7F800000UL) == 0x7F800000UL && (fres[i] & 0x7FFFFF) && (fu.u & 0x7FFFFF)))
      { bad++; if (bad < 30) printf("%d d2f %08lx got %08lx\\n", i, fres[i], fu.u); }
      {
         U back;
         Q e;
         f = fu.f;
         back.d = f;
         {
            /* expected: exact widening of the float */
            unsigned long u = fres[i];
            int s = (int)(u >> 31), ex = (int)((u >> 23) & 0xFF);
            (void)s; (void)ex;
         }
         e = back.q; (void)e;
         chk(i, "f2d", back, &wres[i]);
         back.d = (double)uin[i]; chk(i, "ul2d", back, &ures[i]);
         back.d = (double)sin_[i]; chk(i, "l2d", back, &sres[i]);
         if ((unsigned long)back.d != (unsigned long)sin_[i] && sin_[i] >= 0) { bad++; printf("%d d2ul\\n", i); }
      }
      if (lok[i])
      {
         long l = (long)a.d;
         if (l != lres[i]) { bad++; if (bad < 30) printf("%d d2l %ld got %ld\\n", i, lres[i], l); }
      }
   }
   printf("bad=%d of %d\\n", bad, n);
   return bad != 0;
}''')
