// Translated from MetalBrushCoverage.swift, pinned Compositor a19db9011282399785dc18efcfded904627bdcc2.
// Copyright (c) 2026 Wonder Assembly LLC. MIT license: src/graphics/upstream/LICENSE.
cbuffer BrushUniforms : register(b0) {
    float4 mapping; // a,b,c,d
    float4 geometry; // document origin, radius, hardness
    float4 canvas; // dimensions, antialias width, deposition spacing
    uint4 counts; // tile width,height,newly settled count,total count
};
RWStructuredBuffer<float> permanent : register(u0);
RWStructuredBuffer<uint> preview : register(u1); // D3D11 typed store: one uint per gray8 value
StructuredBuffer<float4> segments : register(t0);
float segmentDistanceSquared(float2 p,float4 segment) {
    float2 v=segment.zw-segment.xy;
    float t=clamp(dot(p-segment.xy,v)/max(dot(v,v),1e-12f),0.0f,1.0f);
    float2 delta=p-(segment.xy+t*v);return dot(delta,delta);
}
float brushCoverage(float squared) {
    float distance=sqrt(squared),radius=geometry.z;
    if(geometry.w>=1.0f)return clamp((radius-distance)/canvas.z+0.5f,0.0f,1.0f);
    float t=clamp((distance/radius-geometry.w)/(1.0f-geometry.w),0.0f,1.0f);
    return max(0.0f,(exp(-2.5f*t*t)-exp(-2.5f))/(1.0f-exp(-2.5f)));
}
float tipDensity(float squared) {return -log(max(1.0f-brushCoverage(squared),0.001f));}
float segmentDensity(float2 p,float4 segment) {
    float2 v=segment.zw-segment.xy;float len=length(v);
    if(len<1e-6f)return tipDensity(dot(p-segment.xy,p-segment.xy));
    float2 direction=v/len;float projection=dot(p-segment.xy,direction);
    float2 perpendicular=p-segment.xy-projection*direction;
    float squared=dot(perpendicular,perpendicular),radiusSquared=geometry.z*geometry.z;
    if(squared>=radiusSquared)return 0.0f;
    float reach=sqrt(radiusSquared-squared),lo=max(0.0f,projection-reach),hi=min(len,projection+reach);
    if(hi<=lo)return 0.0f;
    float midpoint=(lo+hi)*0.5f,halfLength=(hi-lo)*0.5f;
    static const float nodes[4]={0.1834346425f,0.5255324099f,0.7966664774f,0.9602898565f};
    static const float weights[4]={0.3626837834f,0.3137066459f,0.2223810345f,0.1012285363f};
    float integral=0.0f;
    [unroll]for(uint i=0;i<4;++i){float a=midpoint-halfLength*nodes[i]-projection,b=midpoint+halfLength*nodes[i]-projection;
        integral+=weights[i]*(tipDensity(squared+a*a)+tipDensity(squared+b*b));}
    return integral*halfLength/canvas.w;
}
[numthreads(16,16,1)]
void continuousBrush(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixel=dispatchId.xy;if(any(pixel>=counts.xy))return;
    uint index=pixel.y*counts.x+pixel.x;float2 local=float2(pixel)+0.5f;
    float2 p=geometry.xy+local.x*mapping.xy+local.y*mapping.zw;
    if(any(p<0.0f)||any(p>=canvas.xy)){preview[index]=0;return;}
    if(geometry.w>=1.0f){
        float settled=asfloat(0x7f800000),tail=settled;
        for(uint hardCommitted=0;hardCommitted<counts.z;++hardCommitted)settled=min(settled,segmentDistanceSquared(p,segments[hardCommitted]));
        for(uint hardTail=counts.z;hardTail<counts.w;++hardTail)tail=min(tail,segmentDistanceSquared(p,segments[hardTail]));
        float value=max(permanent[index],brushCoverage(settled));permanent[index]=value;
        // Metal round uses halfway-away rounding; HLSL round is ties-to-even.
        preview[index]=(uint)floor(255.0f*max(value,brushCoverage(tail))+0.5f);
    }else{
        float value=permanent[index],tail=0.0f;
        for(uint softCommitted=0;softCommitted<counts.z;++softCommitted)value+=segmentDensity(p,segments[softCommitted]);
        for(uint softTail=counts.z;softTail<counts.w;++softTail)tail+=segmentDensity(p,segments[softTail]);
        permanent[index]=min(value,20.0f);
        preview[index]=(uint)floor(255.0f*(1.0f-exp(-min(value+tail,20.0f)))+0.5f);
    }
}
