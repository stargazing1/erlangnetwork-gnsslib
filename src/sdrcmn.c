/*------------------------------------------------------------------------------
* sdrcmn.c : SDR common functions
*
* Copyright (C) 2014 Taro Suzuki <gnsssdrlib@gmail.com>
* Copyright (C) 2014 T. Takasu <http://www.rtklib.com>
*-----------------------------------------------------------------------------*/
#include "measurement_engine.h"

#define CDIV          32               /* carrier lookup table (cycle) */
#define CMASK         0x1F             /* carrier lookup table mask */
#define CSCALE        (1.0/32.0)       /* carrier lookup table scale (LSB) */

/* get full path from relative path --------------------------------------------
* args   : char *relpath    I   relative path
*          char *fullpath   O   full path
* return : int                  0:success, -1:failure 
*-----------------------------------------------------------------------------*/
extern int getfullpath(char *relpath, char *abspath)
{
    if (realpath(relpath,abspath)==NULL) {
        debug_print("error: getfullpath %s\n",relpath);
        return -1;
    }
    return 0;
}

/* calculation FFT number of points (2^bits samples) ---------------------------
* calculation FFT number of points (round up)
* args   : double x         I   number of points (not 2^bits samples)
*          int    next      I   increment multiplier
* return : int                  FFT number of points (2^bits samples)
*-----------------------------------------------------------------------------*/
extern int calcfftnum(double x, int next)
{
    int nn=(int) ( log(x)/log(2.0) + 0.5 )+next;
    return (int) pow(2.0,nn);
}

/* complex FFT -----------------------------------------------------------------
* cpx=fft(cpx)
* args   : fftw_plan plan  I   fftw plan (NULL: create new plan)
*          cpx_t  *cpx      I/O input/output complex data
*          int    n         I   number of input/output data
* return : none
*-----------------------------------------------------------------------------*/
extern void cpxfft( fftw_plan plan, fftw_complex *cpx, int n)
{
#ifdef FFTMTX
        mlock(hfftmtx);
#endif

    if( plan==NULL ) {

        fftw_plan_with_nthreads(NFFTTHREAD); /* fft execute in multi threads */
        plan = fftw_plan_dft_1d(n,cpx,cpx,FFTW_FORWARD,FFTW_ESTIMATE);
        fftw_execute_dft(plan,cpx,cpx); /* fft */
        fftw_destroy_plan(plan);

    } else {
        fftw_execute_dft(plan,cpx,cpx); /* fft */
    }

#ifdef FFTMTX
        unmlock(hfftmtx);
#endif
}
/* complex IFFT ----------------------------------------------------------------
* cpx=ifft(cpx)
* args   : fftw_plan plan  I   fftw plan (NULL: create new plan)
*          cpx_t  *cpx      I/O input/output complex data
*          int    n         I   number of input/output data
* return : none
*-----------------------------------------------------------------------------*/
extern void cpxifft( fftw_plan plan, fftw_complex *cpx, int n )
{
#ifdef FFTMTX
        mlock(hfftmtx);
#endif

    if( plan == NULL ) {
        
        fftw_plan_with_nthreads( NFFTTHREAD ); /* fft execute in multi threads */
        plan = fftw_plan_dft_1d( n, cpx, cpx, FFTW_BACKWARD, FFTW_ESTIMATE );
        fftw_execute_dft( plan, cpx, cpx ); /* fft */
        fftw_destroy_plan( plan );

    } else {
        fftw_execute_dft( plan, cpx, cpx ); /* fft */
    }

#ifdef FFTMTX
        unmlock(hfftmtx);
#endif

}
/* convert short vector to complex vector --------------------------------------
* cpx=complex(I,Q)
* args   : short  *I        I   input data array (real)
*          short  *Q        I   input data array (imaginary)
*          double scale     I   scale factor
*          int    n         I   number of input data
*          cpx_t *cpx       O   output complex array
* return : none
*-----------------------------------------------------------------------------*/
extern void cpxcpx(const short *II, const short *QQ, double scale, int n, fftw_complex *cpx)
{
    double *p=(double *)cpx;
    int i;

    for (i=0;i<n;i++,p+=2) {
        p[0] = II[i]*(double )scale;
        p[1] = QQ ? QQ[i]*(double )scale : 0.0f;
    }
}
/* convert float vector to complex vector --------------------------------------
* cpx=complex(I,Q)
* args   : float  *I        I   input data array (real)
*          float  *Q        I   input data array (imaginary)
*          double scale     I   scale factor
*          int    n         I   number of input data
*          cpx_t *cpx       O   output complex array
* return : none
*-----------------------------------------------------------------------------*/
extern void cpxcpxf(const float *II, const float *QQ, double scale, int n, fftw_complex *cpx)
{
    double *p=(double  *)cpx;
    int i;

    for (i=0;i<n;i++,p+=2) {
        p[0]=   II[i]*(double)scale;
        p[1]=QQ?QQ[i]*(double)scale:0.0f;
    }
}

/* power spectrum calculation --------------------------------------------------
* power spectrum: pspec=abs(fft(cpx)).^2
* args   : fftw_plan plan  I   fftw plan (NULL: create new plan)
*          cpx_t  *cpx      I   input complex data array
*          int    n         I   number of input data
*          int    flagsum   I   cumulative sum flag (pspec+=pspec)
*          double *pspec    O   output power spectrum data
* return : none
*-----------------------------------------------------------------------------*/
extern void cpxpspec(fftw_plan plan, fftw_complex *cpx, int n, int flagsum, double *pspec)
{
    double *p;
    int i;

    cpxfft(plan,cpx,n); /* fft */

    if (flagsum) { /* cumulative sum */
        for( i=0,p=(double *)cpx; i<n; i++,p+=2)
            pspec[i]+=(p[0]*p[0]+p[1]*p[1]);
    } else {
        for( i=0,p=(double *)cpx; i<n; i++,p+=2)
            pspec[i]=(p[0]*p[0]+p[1]*p[1]);
    }
}
/* 1D interpolation ------------------------------------------------------------
* interpolation of 1D data
* args   : double *x,*y     I   x and y data array
*          int    n         I   number of input data
*          double t         I   interpolation point on x data
* return : double               interpolated y data at t
*-----------------------------------------------------------------------------*/
extern double interp1(double *x, double *y, int n, double t)
{
    int i,j,k,m;
    double z,s,*xx,*yy;
    z=0.0;
    if(n<1) return(z);
    if(n==1) {z=y[0];return(z);}
    if(n==2) {
        z=(y[0]*(t-x[1])-y[1]*(t-x[0]))/(x[0]-x[1]);
        return(z);
    }

    xx=(double*)malloc(sizeof(double)*n);
    yy=(double*)malloc(sizeof(double)*n);
    if (x[0]>x[n-1]) {
        for (j=n-1,k=0;j>=0;j--,k++) {
            xx[k]=x[j];
            yy[k]=y[j];
        }
    } else {
        memcpy(xx,x,sizeof(double)*n);
        memcpy(yy,y,sizeof(double)*n);
    }

    if(t<=xx[1]) {k=0;m=2;}
    else if(t>=xx[n-2]) {k=n-3;m=n-1;}
    else {
        k=1;m=n;
        while (m-k!=1) {
            i=(k+m)/2;
            if (t<xx[i-1]) m=i;
            else k=i;
        }
        k=k-1; m=m-1;
        if(fabs(t-xx[k])<fabs(t-xx[m])) k=k-1;
        else m=m+1;
    }
    z=0.0;
    for (i=k;i<=m;i++) {
        s=1.0;
        for (j=k;j<=m;j++)
            if (j!=i) s=s*(t-xx[j])/(xx[i]-xx[j]);
        z=z+s*yy[i];
    }

    free(xx); free(yy);
    return z;
}
/* convert uint64_t to double --------------------------------------------------
* convert uint64_t array to double array (subtract base value)
* args   : uint64_t *data   I   input uint64_t array
*          uint64_t base    I   base value
*          int    n         I   number of input data
*          double *out      O   output double array
* return : none
*-----------------------------------------------------------------------------*/
extern void uint64todouble(uint64_t *data, uint64_t base, int n, double *out)
{
    int i;
    for (i=0;i<n;i++) out[i]=(double)(data[i]-base);
}

/* maximum value and index (double array) --------------------------------------
* calculate maximum value and index
* args   : double *data     I   input double array
*          int    n         I   number of input data
*          int    exinds    I   exception index (start)
*          int    exinde    I   exception index (end)
*          int    *ind      O   index at maximum value
* return : double               maximum value
* note   : maximum value and index are calculated without exinds-exinde index
*          exinds=exinde=-1: use all data
*-----------------------------------------------------------------------------*/
extern double maxvd(const double *data, int n, int exinds, int exinde, int *ind)
{
    int i;
    double max=data[0];
    *ind=0;
    for(i=1;i<n;i++) {
        if ((exinds<=exinde&&(i<exinds||i>exinde))||
            (exinds> exinde&&(i<exinds&&i>exinde))) {
                if (max<data[i]) {
                    max=data[i];
                    *ind=i;
                }
        }
    }
    return max;
}
/* mean value (double array) ---------------------------------------------------
* calculate mean value
* args   : double *data     I   input double array
*          int    n         I   number of input data
*          int    exinds    I   exception index (start)
*          int    exinde    I   exception index (end)
* return : double               mean value
* note   : mean value is calculated without exinds-exinde index
*          exinds=exinde=-1: use all data
*-----------------------------------------------------------------------------*/
extern double meanvd(const double *data, int n, int exinds, int exinde)
{
    int i,ne=0;
    double mean=0.0;
    for(i=0;i<n;i++) {
        if ((exinds<=exinde)&&(i<exinds||i>exinde)) mean+=data[i];
        else if ((exinds>exinde)&&(i<exinds&&i>exinde)) mean+=data[i];
        else ne++;
    }
    return mean/(n-ne);
}
/* index to subscribe ----------------------------------------------------------
* 1D index to subscribe (index of 2D array)
* args   : int    *ind      I   input data
*          int    nx, ny    I   number of row and column
*          int    *subx     O   subscript index of x
*          int    *suby     O   subscript index of y
* return : none
*-----------------------------------------------------------------------------*/
extern void ind2sub(int ind, int nx, int ny, int *subx, int *suby)
{
    *subx = ind%nx;
    *suby = ny*ind/(nx*ny);
}
/* vector shift function  ------------------------------------------------------
* shift of vector data
* args   : void   *dst      O   output shifted data
*          void   *src      I   input data
*          size_t size      I   type of input data (byte)
*          int    n         I   number of input data
* return : none
*-----------------------------------------------------------------------------*/
extern void shiftdata(void *dst, void *src, size_t size, int n)
{
    void *tmp;
    tmp=malloc(size*n);
    if (tmp!=NULL) {
        memcpy(tmp,src,size*n);
        memcpy(dst,tmp,size*n);
        free(tmp);
    }
}
/* resample code ---------------------------------------------------------------
* resample code
* args   : char   *code     I   code
*          int    len       I   code length (len < 2^(31-FPBIT))
*          double coff      I   initial code offset (chip)
*          int    smax      I   maximum correlator space (sample) 
*          double ci        I   code sampling interval (chip)
*          int    n         I   number of samples
*          short  *rcode    O   resampling code
* return : double               code remainder
*-----------------------------------------------------------------------------*/
extern double rescode(const short *code, int len, double coff, int smax, double ci, int n, short *rcode)
{
    short *p;

    coff-=smax*ci;
    coff-=floor(coff/len)*len; /* 0<=coff<len */

    for (p=rcode;p<rcode+n+2*smax;p++,coff+=ci) {
        if (coff>=len) coff-=len;
        *p=code[(int)coff];
    }
    return coff-smax*ci;
}
/* mix local carrier -----------------------------------------------------------
* mix local carrier to data
* args   : char   *data     I   data
*          int    dtype     I   data type (0:real,1:complex)
*          double ti        I   sampling interval (s)
*          int    n         I   number of samples
*          double freq      I   carrier frequency (Hz)
*          double phi0      I   initial phase (rad)
*          short  *I,*Q     O   carrier mixed data I, Q component
* return : double               phase remainder
*-----------------------------------------------------------------------------*/
extern double mixcarr(const char *data, int dtype, double ti, int n, 
                      double freq, double phi0, short *II, short *QQ)
{
#define CDIV    32
#define DPI     (3.1415926535897932*2.0)
    const char *p;
    double phi,ps,prem;

    static short cost[CDIV]={0},sint[CDIV]={0};
    int i,index;

    /* initialize local carrier table */
    if( !cost[0] ) { 
        for (i=0;i<CDIV;i++) {
            cost[i]=(short)floor((cos(DPI/CDIV*i)/CSCALE+0.5));
            sint[i]=(short)floor((sin(DPI/CDIV*i)/CSCALE+0.5));
        }
    }
    phi=phi0*CDIV/DPI;
    ps=freq*CDIV*ti; /* phase step */

    if (dtype==DTYPEIQ) { /* complex */
        for (p=data;p<data+n*2;p+=2,II++,QQ++,phi+=ps) {
            index=((int)phi)&CMASK;
            *II=cost[index]*p[0]-sint[index]*p[1];
            *QQ=sint[index]*p[0]+cost[index]*p[1];
        }
    }
    if (dtype==DTYPEI) { /* real */
        for (p=data;p<data+n;p++,II++,QQ++,phi+=ps) {
            index=((int)phi)&CMASK;
            *II=cost[index]*p[0];
            *QQ=sint[index]*p[0];
        }
    }
    prem=phi*DPI/CDIV;
    while(prem>DPI) prem-=DPI;
    return prem;

#undef CDIV
}
