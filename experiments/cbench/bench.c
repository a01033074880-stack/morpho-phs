/* Phase-0 CPU-cost microbenchmark (single core, scalar C, -O3 -march=native).
 * Kernels on an L x L scalar square lattice (proposal model), per audio sample:
 *   esav   : corrected explicit SAV-leapfrog, G handled with NJ Jacobi sweeps
 *   esav0  : same, Phi = 0 (no G => fully explicit, 0 sweeps)
 *   lit_dense : proposal SSDG with a precomputed DENSE Cayley matrix Q (2N x 2N), 2 matvecs
 *   rk4    : explicit RK4 (4 force evaluations)
 * Prints ns per sample and % of one core at 48 kHz (20833 ns budget). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#ifndef REAL
#define REAL double
#endif
typedef REAL R;
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+1e-9*t.tv_nsec;}
static int L,N; static R h=1.0/48000;
static R k0,kc,alpha,beta,sig,mu,eta,c0=1;
static inline R logcosh(R x){R a=fabs(x);return a+log1p(exp(-2*a))-0.6931471805599453;}
static inline R Uf(R q){return (R)0.25*alpha*q*q*q*q+beta*logcosh(q);}
static inline R dUf(R q){return alpha*q*q*q+beta*tanh(q);}
static void Kmul(const R*q,R*o){for(int y=0;y<L;y++)for(int x=0;x<L;x++){int i=y*L+x;R s=(k0)*q[i];int d=0;R nb=0;
 if(x>0){nb+=q[i-1];d++;} if(x<L-1){nb+=q[i+1];d++;} if(y>0){nb+=q[i-L];d++;} if(y<L-1){nb+=q[i+L];d++;}
 o[i]=s+kc*(d*q[i]-nb);}}
static void Gmul(const R*v,R*o){for(int y=0;y<L;y++)for(int x=0;x<L;x++){int i=y*L+x;R s=0;
 if(x<L-1)s+=v[i+1]; if(x>0)s-=v[i-1]; if(y<L-1)s+=v[i+L]; if(y>0)s-=v[i-L]; o[i]=sig*s;}}
static R *q,*qm,*ps,*psm,*w,*rhs,*dg,*tmp,*g,*p,*v;
static void esav_step(int NJ){
 Kmul(q,tmp);
 for(int i=0;i<N;i++){R u=Uf(q[i]);R gi=dUf(q[i])/sqrt(2*u+c0);g[i]=gi;R vl=(q[i]-qm[i])/h;R d=-mu+eta*vl*vl;
  dg[i]=1.0/(1.0/(h*h)+0.5*gi*gi+d/(2*h)); rhs[i]=2*(q[i]-qm[i])/(h*h)-tmp[i]-gi*psm[i]; w[i]=rhs[i]*dg[i];}
 for(int k=0;k<NJ;k++){Gmul(w,tmp);for(int i=0;i<N;i++)w[i]=(rhs[i]+tmp[i]/(2*h))*dg[i];}
 for(int i=0;i<N;i++){R qp=qm[i]+w[i];R pp=psm[i]+g[i]*w[i];qm[i]=q[i];q[i]=qp;psm[i]=ps[i];ps[i]=pp;}
}
static R *Q,*xv,*xo;
static void bern(R dt){R a=mu,b=eta,z=2*a*dt,ez=exp(-z),ph=(fabs(z)<1e-5)?1-z/2:(-expm1(-z))/z;R c=2*b*dt*ph;
 for(int i=0;i<N;i++){R pi=xv[N+i];if(pi!=0){xv[N+i]=(pi>0?1:-1)/sqrt(ez/(pi*pi)+c);}}}
static void densemv(){int M=2*N;for(int i=0;i<M;i++){R s=0;const R*row=Q+(size_t)i*M;for(int j=0;j<M;j++)s+=row[j]*xv[j];xo[i]=s;}memcpy(xv,xo,sizeof(R)*M);}
static void lit_step(){bern(h/2);densemv();for(int i=0;i<N;i++)xv[N+i]-=h*dUf(xv[i]);densemv();bern(h/2);}
static void force(const R*qq,const R*pp,R*f){Kmul(qq,tmp);Gmul(pp,f);for(int i=0;i<N;i++){R vv=pp[i];f[i]+= -tmp[i]-dUf(qq[i])-(-mu+eta*vv*vv)*vv;}}
static R *k1q,*k1p,*k2q,*k2p,*k3q,*k3p,*k4q,*k4p,*sq,*sp;
static void rk4_step(){force(q,p,k1p);for(int i=0;i<N;i++){k1q[i]=p[i];sq[i]=q[i]+h/2*k1q[i];sp[i]=p[i]+h/2*k1p[i];}
 force(sq,sp,k2p);for(int i=0;i<N;i++){k2q[i]=sp[i];sq[i]=q[i]+h/2*k2q[i];sp[i]=p[i]+h/2*k2p[i];}
 force(sq,sp,k3p);for(int i=0;i<N;i++){k3q[i]=sp[i];sq[i]=q[i]+h*k3q[i];sp[i]=p[i]+h*k3p[i];}
 force(sq,sp,k4p);for(int i=0;i<N;i++){k4q[i]=sp[i];q[i]+=h/6*(k1q[i]+2*k2q[i]+2*k3q[i]+sp[i]);p[i]+=h/6*(k1p[i]+2*k2p[i]+2*k3p[i]+k4p[i]);}}
#define AL(n) (R*)aligned_alloc(64,((sizeof(R)*(n)+63)/64)*64)
int main(int argc,char**argv){
 L=atoi(argv[1]);const char*mode=argv[2];int NJ=argc>3?atoi(argv[3]):0;N=L*L;
 k0=pow(2*M_PI*150,2);kc=pow(2*M_PI*4000,2)/8;alpha=kc/3;beta=k0;sig=(strcmp(mode,"esav0")==0)?0:2*M_PI*600;mu=60;eta=4*60/(3e6);
 q=AL(N);qm=AL(N);ps=AL(N);psm=AL(N);w=AL(N);rhs=AL(N);dg=AL(N);tmp=AL(N);g=AL(N);p=AL(N);v=AL(N);
 k1q=AL(N);k1p=AL(N);k2q=AL(N);k2p=AL(N);k3q=AL(N);k3p=AL(N);k4q=AL(N);k4p=AL(N);sq=AL(N);sp=AL(N);
 srand(1);for(int i=0;i<N;i++){q[i]=0.1*(rand()/(R)RAND_MAX-0.5);qm[i]=q[i];p[i]=0;ps[i]=sqrt(2*Uf(q[i])+c0);psm[i]=ps[i];}
 int S=48000; double t0,t1;
 if(!strncmp(mode,"esav",4)){for(int n=0;n<4800;n++)esav_step(NJ);t0=now();for(int n=0;n<S;n++)esav_step(NJ);t1=now();}
 else if(!strcmp(mode,"lit_dense")){size_t M=2*N;Q=AL(M*M);xv=AL(M);xo=AL(M);for(size_t i=0;i<M*M;i++)Q[i]=(i%(M+1)==0)?0.999:1e-6;for(size_t i=0;i<M;i++)xv[i]=0.01;
  int SS=(N>=256)?4800:S; for(int n=0;n<480;n++)lit_step();t0=now();for(int n=0;n<SS;n++)lit_step();t1=now();t1=t0+(t1-t0)*S/SS;}
 else {for(int n=0;n<4800;n++)rk4_step();t0=now();for(int n=0;n<S;n++)rk4_step();t1=now();}
 double ns=(t1-t0)/S*1e9; R chk=0;for(int i=0;i<N;i++)chk+=q[i]; if(xv)chk+=xv[0];
 printf("L=%d mode=%s NJ=%d sizeof(R)=%zu  %.1f ns/sample  %.2f%% of one core @48k  (chk %g)\n",L,mode,NJ,sizeof(R),ns,100*ns/20833.33,(double)chk);
 return 0;}
