// Translated from MetalBrushCoverage.swift, pinned Compositor a19db9011282399785dc18efcfded904627bdcc2.
// Pen dynamics follow upstream bf75dd85 (Pen pressure).
// Copyright (c) 2026 Wonder Assembly LLC. MIT license: src/graphics/upstream/LICENSE.
cbuffer BrushUniforms : register(b0) {
    float4 mapping; // a,b,c,d
    float4 geometry; // document origin, radius, hardness
    float4 canvas; // dimensions, antialias width, deposition spacing
    uint4 counts; // tile width,height,newly settled count,total count
    uint4 options; // soft opacity cap, dynamic segment coverage
};
RWStructuredBuffer<float> permanent : register(u0);
RWStructuredBuffer<uint> preview : register(u1); // D3D11 typed store: one uint per gray8 value
RWStructuredBuffer<float> pressureCap : register(u2);
struct BrushSegment {
    float4 endpoints;
    float4 dynamics; // size factors, then strengths, at start and end
};
StructuredBuffer<BrushSegment> segments : register(t0);
float segmentDistanceSquared(float2 p,float4 segment,out float t) {
    float2 v=segment.zw-segment.xy;
    t=clamp(dot(p-segment.xy,v)/max(dot(v,v),1e-12f),0.0f,1.0f);
    float2 delta=p-(segment.xy+t*v);return dot(delta,delta);
}
float brushCoverage(float squared,float radius) {
    float distance=sqrt(squared);
    if(geometry.w>=1.0f)return clamp((radius-distance)/canvas.z+0.5f,0.0f,1.0f);
    float t=clamp((distance/radius-geometry.w)/(1.0f-geometry.w),0.0f,1.0f);
    return max(0.0f,(exp(-2.5f*t*t)-exp(-2.5f))/(1.0f-exp(-2.5f)));
}
float tipDensity(float squared,float radius) {return -log(max(1.0f-brushCoverage(squared,radius),0.001f));}
struct TipSweep {
    float length, projection, perpendicularSquared, radius0, radiusDelta;
};
TipSweep tipSweep(float2 p,BrushSegment s,float radius) {
    float2 offset=p-s.endpoints.xy,v=s.endpoints.zw-s.endpoints.xy;
    float len=length(v);
    float2 direction=len>0.0f ? v/len : float2(1.0f,0.0f);
    float perpendicular=offset.y*direction.x-offset.x*direction.y;
    TipSweep sweep={len,dot(offset,direction),perpendicular*perpendicular,radius*s.dynamics.x,
        radius*(s.dynamics.y-s.dynamics.x)};
    return sweep;
}
float tipRadius(TipSweep sweep,float t) {
    return sweep.radius0+sweep.radiusDelta*t;
}
float tipDistanceSquared(TipSweep sweep,float t) {
    float along=sweep.length*t-sweep.projection;
    return along*along+sweep.perpendicularSquared;
}
bool tipReaches(TipSweep sweep,float t) {
    float radius=tipRadius(sweep,t);
    return tipDistanceSquared(sweep,t)<radius*radius;
}
float tipReachLimit(TipSweep sweep,float inside,float endpoint) {
    if(tipReaches(sweep,endpoint))return endpoint;
    float outside=endpoint;
    [loop]for(int i=0;i<20;++i){
        float mid=(inside+outside)*0.5f;
        if(tipReaches(sweep,mid))inside=mid;
        else outside=mid;
    }
    return inside;
}
float closestTipEdge(TipSweep sweep) {
    float change=abs(sweep.radiusDelta);
    if(change>=sweep.length)return sweep.radiusDelta>0.0f ? 1.0f : 0.0f;
    float tangent=sqrt((sweep.length-change)*(sweep.length+change));
    return saturate((sweep.projection+sqrt(sweep.perpendicularSquared)*sweep.radiusDelta/tangent)/sweep.length);
}
float hardTipCoverage(TipSweep sweep,float t) {
    return brushCoverage(tipDistanceSquared(sweep,t),tipRadius(sweep,t));
}
float hardCoverage(float2 p,BrushSegment s) {
    TipSweep sweep=tipSweep(p,s,geometry.z);
    float closest=closestTipEdge(sweep),strongest=s.dynamics.w>s.dynamics.z ? 1.0f : 0.0f;
    float maximumTip=hardTipCoverage(sweep,closest);
    float best=maximumTip*lerp(s.dynamics.z,s.dynamics.w,closest);
    if(maximumTip==0.0f || closest==strongest || s.dynamics.z==s.dynamics.w)return best;

    // Keep the search and iteration count aligned with the CPU brush.
    float lo=0.0f,hi=1.0f;
    best=max(best,hardTipCoverage(sweep,strongest)*lerp(s.dynamics.z,s.dynamics.w,strongest));
    [loop]for(int i=0;i<24;++i){
        float left=lo+(hi-lo)/3.0f,right=hi-(hi-lo)/3.0f;
        float a=lerp(closest,strongest,left),b=lerp(closest,strongest,right);
        float scoreA=hardTipCoverage(sweep,a)*lerp(s.dynamics.z,s.dynamics.w,a);
        float scoreB=hardTipCoverage(sweep,b)*lerp(s.dynamics.z,s.dynamics.w,b);
        best=max(best,max(scoreA,scoreB));
        if(scoreA<scoreB)lo=left;
        else hi=right;
    }
    return best;
}
float segmentStrength(float2 p,BrushSegment s) {
    TipSweep sweep=tipSweep(p,s,geometry.z);
    float strongest=s.dynamics.w>s.dynamics.z ? 1.0f : 0.0f;
    if(tipReaches(sweep,strongest))return max(s.dynamics.z,s.dynamics.w);
    float inside=closestTipEdge(sweep);
    if(!tipReaches(sweep,inside))return 0.0f;
    if(s.dynamics.z==s.dynamics.w)return s.dynamics.z;
    return lerp(s.dynamics.z,s.dynamics.w,tipReachLimit(sweep,inside,strongest));
}
float segmentDensity(float2 p,BrushSegment s) {
    if(s.dynamics.z==0.0f && s.dynamics.w==0.0f)return 0.0f;
    float2 offset=p-s.endpoints.xy;
    float2 v=s.endpoints.zw-s.endpoints.xy;float len=length(v);
    if(len<1e-6f)return tipDensity(dot(offset,offset),geometry.z*s.dynamics.x);
    float2 direction=v/len;float projection=dot(offset,direction);
    float2 perpendicular=offset-projection*direction;
    float widest=geometry.z*max(s.dynamics.x,s.dynamics.y);
    float squared=dot(perpendicular,perpendicular),radiusSquared=widest*widest;
    if(squared>=radiusSquared)return 0.0f;
    float reach=sqrt(radiusSquared-squared),lo=max(0.0f,projection-reach),hi=min(len,projection+reach);
    if(hi<=lo)return 0.0f;
    bool variableSize=s.dynamics.x!=s.dynamics.y;
    float radiusDelta=geometry.z*(s.dynamics.y-s.dynamics.x);
    int panels=1;
    if(variableSize){
        TipSweep sweep={len,projection,squared,geometry.z*s.dynamics.x,radiusDelta};
        float inside=closestTipEdge(sweep);
        if(!tipReaches(sweep,inside))return 0.0f;
        lo=len*tipReachLimit(sweep,inside,0.0f);
        hi=len*tipReachLimit(sweep,inside,1.0f);
        panels=abs(radiusDelta)>=len ? 3 : 2;
    }
    if(hi<=lo)return 0.0f;
    float halfLength=(hi-lo)/(2.0f*panels);
    static const float nodes[4]={0.1834346425f,0.5255324099f,0.7966664774f,0.9602898565f};
    static const float weights[4]={0.3626837834f,0.3137066459f,0.2223810345f,0.1012285363f};
    float integral=0.0f;
    [loop]for(int panel=0;panel<panels;++panel){
        float midpoint=variableSize?lo+(2.0f*panel+1.0f)*halfLength:(lo+hi)*0.5f;
        [unroll]for(uint i=0;i<4;++i){
            float a=midpoint-halfLength*nodes[i]-projection,b=midpoint+halfLength*nodes[i]-projection;
            float sizeA=variableSize?lerp(s.dynamics.x,s.dynamics.y,saturate((projection+a)/len)):s.dynamics.x;
            float sizeB=variableSize?lerp(s.dynamics.x,s.dynamics.y,saturate((projection+b)/len)):s.dynamics.x;
            float densityA=tipDensity(squared+a*a,geometry.z*sizeA);
            float densityB=tipDensity(squared+b*b,geometry.z*sizeB);
            if(variableSize){
                densityA/=max(0.25f,canvas.w*sizeA);
                densityB/=max(0.25f,canvas.w*sizeB);
            }
            integral+=weights[i]*(densityA+densityB);
        }
    }
    float spacing=variableSize?1.0f:max(0.25f,canvas.w*s.dynamics.x);
    return integral*halfLength/spacing;
}
[numthreads(16,16,1)]
void continuousBrush(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixel=dispatchId.xy;if(any(pixel>=counts.xy))return;
    uint index=pixel.y*counts.x+pixel.x;float2 local=float2(pixel)+0.5f;
    float2 p=geometry.xy+local.x*mapping.xy+local.y*mapping.zw;
    if(any(p<0.0f)||any(p>=canvas.xy)){preview[index]=0;return;}
    if(geometry.w>=1.0f && options.y==0){
        float a=asfloat(0x7f800000),b=a,t;
        for(uint i=0;i<counts.z;++i)a=min(a,segmentDistanceSquared(p,segments[i].endpoints,t));
        for(uint j=counts.z;j<counts.w;++j)b=min(b,segmentDistanceSquared(p,segments[j].endpoints,t));
        float value=max(permanent[index],brushCoverage(a,geometry.z));
        permanent[index]=value;
        preview[index]=(uint)floor(255.0f*max(value,brushCoverage(b,geometry.z))+0.5f);
    }else if(geometry.w>=1.0f){
        float value=permanent[index];
        for(uint hardCommitted=0;hardCommitted<counts.z;++hardCommitted)value=max(value,hardCoverage(p,segments[hardCommitted]));
        permanent[index]=value;
        for(uint hardTail=counts.z;hardTail<counts.w;++hardTail)value=max(value,hardCoverage(p,segments[hardTail]));
        // Metal round uses halfway-away rounding; HLSL round is ties-to-even.
        preview[index]=(uint)floor(255.0f*value+0.5f);
    }else{
        float value=permanent[index],tail=0.0f;
        for(uint softCommitted=0;softCommitted<counts.z;++softCommitted)value+=segmentDensity(p,segments[softCommitted]);
        for(uint softTail=counts.z;softTail<counts.w;++softTail)tail+=segmentDensity(p,segments[softTail]);
        permanent[index]=min(value,20.0f);
        float strength=1.0f;
        if(options.x!=0){
            strength=pressureCap[index];
            for(uint capCommitted=0;capCommitted<counts.z;++capCommitted)strength=max(strength,segmentStrength(p,segments[capCommitted]));
            pressureCap[index]=strength;
            for(uint capTail=counts.z;capTail<counts.w;++capTail)strength=max(strength,segmentStrength(p,segments[capTail]));
        }
        preview[index]=(uint)floor(255.0f*(strength*(1.0f-exp(-min(value+tail,20.0f))))+0.5f);
    }
}
