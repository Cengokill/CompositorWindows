std::vector<BrushSegment> brushContinuousCurve(Point start,Point end,Point before,Point after){
    auto knot=[](double t,Point a,Point b){return t+std::max(0.0001,std::sqrt(std::hypot(b.x-a.x,b.y-a.y)));};
    auto mix=[](Point a,Point b,double ta,double tb,double t){double wa=(tb-t)/(tb-ta),wb=(t-ta)/(tb-ta);return Point{a.x*wa+b.x*wb,a.y*wa+b.y*wb};};
    double t0=0,t1=knot(t0,before,start),t2=knot(t1,start,end),t3=knot(t2,end,after);
    auto point=[&](double u){if(u==0)return start;if(u==1)return end;double t=t1+(t2-t1)*u;
        auto a=mix(before,start,t0,t1,t),b=mix(start,end,t1,t2,t),c=mix(end,after,t2,t3,t);
        return mix(mix(a,b,t0,t2,t),mix(b,c,t1,t3,t),t1,t2,t);};
    std::vector<BrushSegment> result;
    auto subdivide=[&](auto&& self,Point a,Point b,double lo,double hi,int depth)->void {
        double dx=b.x-a.x,dy=b.y-a.y,squared=dx*dx+dy*dy;
        auto error=[&](Point p){double t=squared>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/squared,0.,1.):0;
            return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);};
        double mid=(lo+hi)/2;auto m=point(mid);
        double deviation=std::max({error(m),error(point((lo+mid)/2)),error(point((mid+hi)/2))});
        if(deviation<=0.2||depth>=10){result.push_back(segment(a,b));return;}
        self(self,a,m,lo,mid,depth+1);self(self,m,b,mid,hi,depth+1);
    };
    subdivide(subdivide,start,end,0,1,0);return result;
}
