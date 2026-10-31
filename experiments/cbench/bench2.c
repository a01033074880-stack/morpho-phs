/* Optimised single-core kernels (float, padded halo, vectorisable loops).
 * build: gcc -O3 -march=native -ffast-math -fopenmp-simd bench2.c -lm
 * modes: esav NJ | rk4 | lit_dense | gs (Gray-Scott step cost)
 * FASTMATH=1 uses polynomial/rational tanh, exp, log1p approximations (plugin practice). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
typedef float R;
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec;}
static int L,W,NP; static R h=1.0f/48000;
static R k0,kc,alpha,beta,sig,mu,eta,c0=1;
#ifdef FAST
static inline R ftanh(R x){x=fminf(fmaxf(x,-4.97f),4.97f);R x2=x*x;R n=x*(135135.f+x2*(17325.f+x2*(378.f+x2)));R d=135135.f+x2*(62370.f+x2*(3150.f+x2*28.f));return n/d;}
static inline R fexp_neg(R x){/* exp(x), x<=0 */ x=fmaxf(x,-80.f); R t=x*1.442695041f; R fi=floorf(t); R f=t-fi; R p=1.f+f*(0.6931472f+f*(0.2402265f+f*(0.05550411f+f*(0.009618129f+f*0.001333355f))));
 union{float f;int i;}u; u.i=((int)fi+127)<<23; return p*u.f;}
static inline R flog1p01(R y){/* log1p(y), y in [0,1] */ return y*(0.99949556f+y*(-0.49190896f+y*(0.28947478f+y*(-0.13606275f+y*0.03215845f))));}
/* small-|x| branch: absolute error of the generic form (~4e-5) times beta~9e5 would make 2U+c0<0 -> NaN in sqrt (found in Phase 0) */
static inline R logcosh(R x){R a=fabsf(x);R x2=x*x;R s=x2*(0.5f+x2*(-1.f/12+x2*(1.f/45)));R gnr=a+flog1p01(fexp_neg(-2*a))-0.6931472f;return a<0.25f?s:gnr;}
#define TANH ftanh
#else
static inline R logcosh(R x){R a=fabsf(x);return a+log1pf(expf(-2*a))-0.6931472f;}
#define TANH tanhf
#endif
static R *q,*qm,*ps,*psm,*w,*rhs,*dg,*t1,*g,*deg,*mask,*p;
#define IX(x,y) ((y)*W+(x))
static inline void Kmul(const R*restrict a,R*restrict o){
 #pragma omp simd
 for(int i=W;i<NP-W;i++) o[i]=mask[i]*(k0*a[i]+kc*(deg[i]*a[i]-a[i-1]-a[i+1]-a[i-W]-a[i+W]));}
static inline void Gmul(const R*restrict a,R*restrict o){
 #pragma omp simd
 for(int i=W;i<NP-W;i++) o[i]=mask[i]*sig*(a[i+1]-a[i-1]+a[i+W]-a[i-W]);}
static void esav_step(int NJ){
 Kmul(q,t1);
 #pragma omp simd
 for(int i=W;i<NP-W;i++){R qi=q[i];R u=0.25f*alpha*qi*qi*qi*qi+beta*logcosh(qi);R gi=(alpha*qi*qi*qi+beta*TANH(qi))/sqrtf(2*u+c0);g[i]=gi;
  R vl=(qi-qm[i])/h;R d=-mu+eta*vl*vl;R di=mask[i]/(1.f/(h*h)+0.5f*gi*gi+d/(2*h));dg[i]=di;R r=2*(qi-qm[i])/(h*h)-t1[i]-gi*psm[i];rhs[i]=r;w[i]=r*di;}
 for(int k=0;k<NJ;k++){Gmul(w,t1);
  #pragma omp simd
  for(int i=W;i<NP-W;i++)w[i]=(rhs[i]+t1[i]/(2*h))*dg[i];}
 #pragma omp simd
 for(int i=W;i<NP-W;i++){R qp=qm[i]+w[i];R pp=psm[i]+g[i]*w[i];qm[i]=q[i];q[i]=qp;psm[i]=ps[i];ps[i]=pp;}
}
/* RK4 */
static R *f1,*f2,*f3,*f4,*sq,*sp,*kq2,*kq3;
static void force(const R*qq,const R*pp,R*f){Kmul(qq,t1);Gmul(pp,f);
 #pragma omp simd
 for(int i=W;i<NP-W;i++){R vv=pp[i],x=qq[i];f[i]=mask[i]*(f[i]-t1[i]-(alpha*x*x*x+beta*TANH(x))-(-mu+eta*vv*vv)*vv);}}
static void rk4_step(){force(q,p,f1);
 #pragma omp simd
 for(int i=0;i<NP;i++){sq[i]=q[i]+h/2*p[i];sp[i]=p[i]+h/2*f1[i];}
 force(sq,sp,f2);
 #pragma omp simd
 for(int i=0;i<NP;i++){kq2[i]=sp[i];sq[i]=q[i]+h/2*sp[i];sp[i]=p[i]+h/2*f2[i];}
 force(sq,sp,f3);
 #pragma omp simd
 for(int i=0;i<NP;i++){kq3[i]=sp[i];sq[i]=q[i]+h*sp[i];sp[i]=p[i]+h*f3[i];}
 force(sq,sp,f4);
 #pragma omp simd
 for(int i=0;i<NP;i++){q[i]+=h/6*(p[i]+2*kq2[i]+2*kq3[i]+sp[i]);p[i]+=h/6*(f1[i]+2*f2[i]+2*f3[i]+f4[i]);}}
/* literal with dense Q */
static R *Q,*xv,*xo; static int N,M;
static void densemv(){for(int i=0;i<M;i++){R s=0;const R*row=Q+(size_t)i*M;
 #pragma omp simd reduction(+:s)
 for(int j=0;j<M;j++)s+=row[j]*xv[j];xo[i]=s;}memcpy(xv,xo,sizeof(R)*M);}
static void bern(R dt){R z=2*mu*dt,ez=expf(-z),c=2*eta*dt*(-expm1f(-z))/z;
 #pragma omp simd
 for(int i=0;i<N;i++){R pi=xv[N+i];xv[N+i]=(pi==0)?0:copysignf(1.f,pi)/sqrtf(ez/(pi*pi)+c);}}
static void lit_step(){bern(h/2);densemv();
 #pragma omp simd
 for(int i=0;i<N;i++){R x=xv[i];xv[N+i]-=h*(alpha*x*x*x+beta*TANH(x));}
 densemv();bern(h/2);}
#define AL(n) (R*)aligned_alloc(64,((sizeof(R)*(n)+63)/64)*64)
int main(int argc,char**argv){
 L=atoi(argv[1]);const char*mode=argv[2];int NJ=argc>3?atoi(argv[3]):0;W=L+2;NP=W*W;N=L*L;M=2*N;
 k0=powf(2*M_PI*150,2);kc=powf(2*M_PI*4000,2)/8;alpha=kc/3;beta=k0;sig=(NJ==0&&!strcmp(mode,"esav"))?0:2*M_PI*600;mu=60;eta=4*60/(3e6);
 R**arrs[]={&q,&qm,&ps,&psm,&w,&rhs,&dg,&t1,&g,&deg,&mask,&p,&f1,&f2,&f3,&f4,&sq,&sp,&kq2,&kq3};
 for(unsigned a=0;a<sizeof(arrs)/sizeof(*arrs);a++){*arrs[a]=AL(NP);memset(*arrs[a],0,sizeof(R)*NP);}
 srand(1);for(int y=1;y<=L;y++)for(int x=1;x<=L;x++){int i=IX(x,y);mask[i]=1;deg[i]=(x>1)+(x<L)+(y>1)+(y<L);q[i]=0.1f*(rand()/(R)RAND_MAX-0.5f);qm[i]=q[i];}
 for(int i=0;i<NP;i++){R u=0.25f*alpha*powf(q[i],4)+beta*logcosh(q[i]);ps[i]=psm[i]=sqrtf(2*u+c0);}
 int S=48000,SS=S; double ta=0,tb=0;
 if(!strcmp(mode,"esav")){for(int n=0;n<4800;n++)esav_step(NJ);ta=now();for(int n=0;n<S;n++)esav_step(NJ);tb=now();}
 else if(!strcmp(mode,"rk4")){for(int n=0;n<4800;n++)rk4_step();ta=now();for(int n=0;n<S;n++)rk4_step();tb=now();}
 else if(!strcmp(mode,"lit_dense")){Q=AL((size_t)M*M);xv=AL(M);xo=AL(M);for(size_t i=0;i<(size_t)M*M;i++)Q[i]=(i%(M+1)==0)?0.999f:1e-6f;for(int i=0;i<M;i++)xv[i]=0.01f;
  SS=(N>=1024)?240:(N>=256?4800:S);for(int n=0;n<100;n++)lit_step();ta=now();for(int n=0;n<SS;n++)lit_step();tb=now();}
 else if(!strcmp(mode,"gs")){/* one GS RD step per 64 samples, 2 fields, 5-pt Laplacian, Euler */
  R*u=AL(NP),*v=AL(NP),*un=AL(NP),*vn=AL(NP);for(int i=0;i<NP;i++){u[i]=1;v[i]=0.01f*(rand()%2);}R Du=0.2f,Dv=0.1f,F=0.04f,K=0.06f,dt=1;
  SS=S/64*10;ta=now();for(int n=0;n<SS;n++){
   #pragma omp simd
   for(int i=W;i<NP-W;i++){R lu=u[i-1]+u[i+1]+u[i-W]+u[i+W]-4*u[i],lv=v[i-1]+v[i+1]+v[i-W]+v[i+W]-4*v[i];R uvv=u[i]*v[i]*v[i];
    un[i]=mask[i]*(u[i]+dt*(Du*lu-uvv+F*(1-u[i])));vn[i]=mask[i]*(v[i]+dt*(Dv*lv+uvv-(F+K)*v[i]));}
   R*tt=u;u=un;un=tt;tt=v;v=vn;vn=tt;}tb=now();
  double ns=(tb-ta)/SS*1e9;printf("L=%d mode=gs  %.1f ns per RD step -> %.2f ns/audio-sample amortised over 64 (%.4f%% core)\n",L,ns,ns/64,100*ns/64/20833.33);return 0;}
 double ns=(tb-ta)/SS*1e9;R chk=0;for(int i=0;i<NP;i++)chk+=q[i];if(xv)chk+=xv[0];
 printf("L=%d mode=%s NJ=%d %s  %.1f ns/sample  %.2f%% of one core @48k (chk %g)\n",L,mode,NJ,
#ifdef FAST
 "fastapprox",
#else
 "libm",
#endif
 ns,100*ns/20833.33,(double)chk);return 0;}
