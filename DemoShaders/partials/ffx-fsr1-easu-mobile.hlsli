#ifndef FFX_FSR1_EASU_MOBILE_HLSLI
#define FFX_FSR1_EASU_MOBILE_HLSLI

//==============================================================================================================================
// An optimized AMD FSR's EASU implementation for Mobiles  -- HLSL (SM5.0) PORT
// Based on https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
// Details can be found: https://atyuwen.github.io/posts/optimizing-fsr/
// Distributed under the MIT License.
//
// Requires ffx-a.hlsli and the FSR_EASU_F callbacks (FsrEasuRF/GF/BF) declared by ffx-fsr1.hlsli,
// plus the additional callback FsrEasuSampleF (declared below).
//
// GLSL precision qualifiers (highp / mediump) were dropped; HLSL has no equivalent for SM5.0.
// GLSL had: #define rcp(x) (1.0/(x)) and #define rsqrt inversesqrt -- HLSL provides rcp()/rsqrt()
// as intrinsics, so those redefinitions are omitted.
//
// Configuration:
//      FILAMENT_FSR_DERINGING: if defined, performs deringing code.
//==============================================================================================================================

AF3 FsrEasuSampleF(AF2 p);

void FsrEasuTapF2(
        inout AF2 aCR, inout AF2 aCG, inout AF2 aCB,
        inout AF2 aW,
        AF2 offX, AF2 offY,
        AF2 dir,
        AF2 len,
        AF1 lob,
        AF1 clp,
        AF2 cR, AF2 cG, AF2 cB) {
    AF2 vX,vY;
    vX=offX*  dir.xx +offY*dir.yy;
    vY=offX*(-dir.yy)+offY*dir.xx;
    vX*=len.x;vY*=len.y;
    AF2 d2=vX*vX+vY*vY;
    d2=min(d2,AF2_(clp));
    AF2 wB=AF2_(2.0/5.0)*d2+AF2_(-1.0);
    AF2 wA=AF2_(lob)*d2+AF2_(-1.0);
    wB*=wB;
    wA*=wA;
    wB=AF2_(25.0/16.0)*wB+AF2_(-(25.0/16.0-1.0));
    AF2 w=wB*wA;
    aCR+=cR*w;aCG+=cG*w;aCB+=cB*w;aW+=w;
}

void FsrEasuLF(
        out AF3 pix,
        AF2 ip,
        AF4 con0,
        AF4 con1,
        AF4 con2,
        AF4 con3){
    //------------------------------------------------------------------------------------------------------------------------------
    // Direction is the '+' diff.
    //    A
    //  B C D
    //    E
    AF2 pp=(ip)*(con0.xy)+(con0.zw);
    AF2 tc=(pp+AF2_(0.5))*con1.xy;
    AF3 sA=FsrEasuSampleF(tc-AF2(0, con1.y));
    AF3 sB=FsrEasuSampleF(tc-AF2(con1.x, 0));
    AF3 sC=FsrEasuSampleF(tc);
    AF3 sD=FsrEasuSampleF(tc+AF2(con1.x, 0));
    AF3 sE=FsrEasuSampleF(tc+AF2(0, con1.y));
    AF1 lA=sA.r*0.5+sA.g;
    AF1 lB=sB.r*0.5+sB.g;
    AF1 lC=sC.r*0.5+sC.g;
    AF1 lD=sD.r*0.5+sD.g;
    AF1 lE=sE.r*0.5+sE.g;
    // Then takes magnitude from abs average of both sides of 'C'.
    AF1 dc=lD-lC;
    AF1 cb=lC-lB;
    AF1 lenX=max(abs(dc),abs(cb));
    lenX=ARcpF1(lenX);
    AF1 dirX=lD-lB;
    lenX=ASatF1(abs(dirX)*lenX);
    lenX*=lenX;
    // Repeat for the y axis.
    AF1 ec=lE-lC;
    AF1 ca=lC-lA;
    AF1 lenY=max(abs(ec),abs(ca));
    lenY=ARcpF1(lenY);
    AF1 dirY=lE-lA;
    lenY=ASatF1(abs(dirY)*lenY);
    AF1 len = lenY * lenY + lenX;
    AF2 dir = AF2(dirX, dirY);
    //------------------------------------------------------------------------------------------------------------------------------
    AF2 dir2=dir*dir;
    AF1 dirR=dir2.x+dir2.y;
    if (dirR<AF1_(1.0/64.0)) {
        pix = sC;
        return;
    }
    dirR=rsqrt(dirR);
    dir*=AF2_(dirR);
    len=len*AF1_(0.5);
    len*=len;
    AF1 stretch=(dir.x*dir.x+dir.y*dir.y)*rcp(max(abs(dir.x),abs(dir.y)));
    AF2 len2=AF2(AF1_(1.0)+(stretch-AF1_(1.0))*len,AF1_(1.0)+AF1_(-0.5)*len);
    AF1 lob=AF1_(0.5)+AF1_((1.0/4.0-0.04)-0.5)*len;
    AF1 clp=rcp(lob);
    //------------------------------------------------------------------------------------------------------------------------------
    AF2 fp=floor(pp);
    pp-=fp;
    AF2 ppp=AF2(pp);
    AF2 p0=fp*(con1.xy)+(con1.zw);
    AF2 p1=p0+(con2.xy);
    AF2 p2=p0+(con2.zw);
    AF2 p3=p0+(con3.xy);
    p0.y-=con1.w; p3.y+=con1.w;
    AF4 fgcbR=FsrEasuRF(p0);
    AF4 fgcbG=FsrEasuGF(p0);
    AF4 fgcbB=FsrEasuBF(p0);
    AF4 ijfeR=FsrEasuRF(p1);
    AF4 ijfeG=FsrEasuGF(p1);
    AF4 ijfeB=FsrEasuBF(p1);
    AF4 klhgR=FsrEasuRF(p2);
    AF4 klhgG=FsrEasuGF(p2);
    AF4 klhgB=FsrEasuBF(p2);
    AF4 nokjR=FsrEasuRF(p3);
    AF4 nokjG=FsrEasuGF(p3);
    AF4 nokjB=FsrEasuBF(p3);
    //------------------------------------------------------------------------------------------------------------------------------

#ifdef FILAMENT_FSR_DERINGING
    AF3 min4=min(AMin3F3(AF3(ijfeR.z,ijfeG.z,ijfeB.z),AF3(klhgR.w,klhgG.w,klhgB.w),AF3(ijfeR.y,ijfeG.y,ijfeB.y)),
    AF3(klhgR.x,klhgG.x,klhgB.x));
    AF3 max4=max(AMax3F3(AF3(ijfeR.z,ijfeG.z,ijfeB.z),AF3(klhgR.w,klhgG.w,klhgB.w),AF3(ijfeR.y,ijfeG.y,ijfeB.y)),
    AF3(klhgR.x,klhgG.x,klhgB.x));
#endif

    AF2 pR=AF2_(0.0);
    AF2 pG=AF2_(0.0);
    AF2 pB=AF2_(0.0);
    AF2 pW=AF2_(0.0);
    FsrEasuTapF2(pR,pG,pB,pW,AF2( 1.0, 0.0)-ppp.xx,AF2(-1.0,-1.0)-ppp.yy,dir,len2,lob,clp,fgcbR.zw,fgcbG.zw,fgcbB.zw);
    FsrEasuTapF2(pR,pG,pB,pW,AF2(-1.0, 0.0)-ppp.xx,AF2( 1.0, 1.0)-ppp.yy,dir,len2,lob,clp,ijfeR.xy,ijfeG.xy,ijfeB.xy);
    FsrEasuTapF2(pR,pG,pB,pW,AF2( 0.0,-1.0)-ppp.xx,AF2( 0.0, 0.0)-ppp.yy,dir,len2,lob,clp,ijfeR.zw,ijfeG.zw,ijfeB.zw);
    FsrEasuTapF2(pR,pG,pB,pW,AF2( 1.0, 2.0)-ppp.xx,AF2( 1.0, 1.0)-ppp.yy,dir,len2,lob,clp,klhgR.xy,klhgG.xy,klhgB.xy);
    FsrEasuTapF2(pR,pG,pB,pW,AF2( 2.0, 1.0)-ppp.xx,AF2( 0.0, 0.0)-ppp.yy,dir,len2,lob,clp,klhgR.zw,klhgG.zw,klhgB.zw);
    FsrEasuTapF2(pR,pG,pB,pW,AF2( 0.0, 1.0)-ppp.xx,AF2( 2.0, 2.0)-ppp.yy,dir,len2,lob,clp,nokjR.xy,nokjG.xy,nokjB.xy);
    AF3 aC=AF3(pR.x+pR.y,pG.x+pG.y,pB.x+pB.y);
    AF1 aW=pW.x+pW.y;
    //------------------------------------------------------------------------------------------------------------------------------

#ifdef FILAMENT_FSR_DERINGING
    pix=min(max4,max(min4,aC*AF3_(ARcpF1(aW))));
#else
    pix=aC*AF3_(ARcpF1(aW));
#endif
}

void FsrEasuL(
        out AF3 pix,
        AU2 ip,
        AU4 con0,
        AU4 con1,
        AU4 con2,
        AU4 con3)
{
    FsrEasuLF(pix, AF2(ip), AF4_AU4(con0), AF4_AU4(con1), AF4_AU4(con2), AF4_AU4(con3));
}

#endif // FFX_FSR1_EASU_MOBILE_HLSLI
