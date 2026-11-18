#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "softfloat.c"
static dbl tod(double x){uint64_t u;memcpy(&u,&x,8);dbl d;d.hi=u>>32;d.lo=(uint32_t)u;return d;}
static double fromd(dbl d){uint64_t u=((uint64_t)d.hi<<32)|d.lo;double x;memcpy(&x,&u,8);return x;}
static uint64_t bits(double x){uint64_t u;memcpy(&u,&x,8);return u;}
static uint64_t rnd64(void){uint64_t r=0;for(int i=0;i<4;i++)r=(r<<16)^(rand()&0xffff);return r;}
static double rnddbl(int mode){
  uint64_t u=rnd64();
  if(mode==1){ /* moderate exponents */ u=(u&0x800FFFFFFFFFFFFFULL)|((uint64_t)(1023-40+rand()%80)<<52);}
  if(mode==2){ /* tiny/huge */ u=(u&0x800FFFFFFFFFFFFFULL)|((uint64_t)(rand()%2?rand()%60:2047-1-rand()%60)<<52);}
  double x; memcpy(&x,&u,8); return x;}
int bad=0;
static void chk(const char*op,double a,double b,double got,double exp){
  if(isnan(exp)&&isnan(got))return;
  if(bits(got)!=bits(exp)){ if(bad<20)printf("%s %a %a: got %a exp %a\n",op,a,b,got,exp); bad++;}}
int main(){
  double sp[]={0.0,-0.0,1.0,-1.0,0.5,2.0,3.0,1e308,-1e308,4.9e-324,2.2250738585072014e-308,INFINITY,-INFINITY,NAN,1.5,0.1,1e-300,123456789.0};
  int ns=sizeof sp/sizeof sp[0];
  for(int m=0;m<4;m++)
  for(int i=0;i<300000;i++){
    double a,b;
    if(m==3){a=sp[rand()%ns];b=sp[rand()%ns];} else {a=rnddbl(m);b=rnddbl(m);}
    if(m==1&&i%3==0) b=a*(1+ldexp(rnddbl(0)>0?1:-1, -(rand()%60)));
    dbl x=tod(a),y=tod(b),r;
    __dadd(&r,&x,&y); chk("add",a,b,fromd(r),a+b);
    __dsub(&r,&x,&y); chk("sub",a,b,fromd(r),a-b);
    __dmul(&r,&x,&y); chk("mul",a,b,fromd(r),a*b);
    __ddiv(&r,&x,&y); chk("div",a,b,fromd(r),a/b);
    int c=__dcmp(&x,&y); int e= (isnan(a)||isnan(b))?2: a<b?-1: a>b?1:0;
    if(c!=e){ if(bad<20)printf("cmp %a %a got %d exp %d\n",a,b,c,e); bad++;}
    float f=(float)a; uint32_t fb; memcpy(&fb,&f,4);
    uint32_t gf=__d2f(&x); if(gf!=fb && !(isnan(a))){ if(bad<20)printf("d2f %a got %08x exp %08x\n",a,gf,fb); bad++;}
    dbl fd; __f2d(&fd,fb); if(!isnan(f) && bits(fromd(fd))!=bits((double)f)){ if(bad<20)printf("f2d %a\n",(double)f); bad++;}
    int32_t li=(int32_t)(rnd64()); dbl ld; __l2d(&ld,li); if(fromd(ld)!=(double)li){bad++; if(bad<20)printf("l2d %d\n",li);}
    uint32_t ui=(uint32_t)(rnd64()); __ul2d(&ld,ui); if(fromd(ld)!=(double)ui){bad++; if(bad<20)printf("ul2d %u\n",ui);}
    if(fabs(a)<2e9 && !isnan(a)){ int32_t t=__d2l(&x); double tr=trunc(a); if(tr>=-2147483648.0&&tr<=2147483647.0 && t!=(int32_t)tr){bad++; if(bad<20)printf("d2l %a got %d\n",a,t);} }
    if(a>=0 && a<4e9){ uint32_t t=__d2ul(&x); if(t!=(uint32_t)a){bad++; if(bad<20)printf("d2ul %a got %u\n",a,t);} }
  }
  printf("bad=%d\n",bad); return bad!=0;
}
