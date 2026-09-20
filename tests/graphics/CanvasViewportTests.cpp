#include "graphics/CanvasViewport.h"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace compositor;
using compositor::graphics::CanvasViewport;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message){require(std::abs(a-b)<1e-9,message);}
void near(Point a,Point b,const char* message){near(a.x,b.x,message);near(a.y,b.y,message);}
}
int main(){int passed=0,failed=0;auto test=[&](const char* name,const std::function<void()>& run){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("viewport_physical_pixel_at_dpr2",[]{CanvasViewport v;v.resize({800,600},2);v.setZoom(1,{400,300},{640,480});near(v.pointsPerPixel(),.5,"Actual Pixels used logical pixels");const auto a=v.viewPoint({40,60},{640,480}),b=v.viewPoint({41,61},{640,480});near((b.x-a.x)*v.backingScale,1,"Horizontal physical pixel mismatch");near((b.y-a.y)*v.backingScale,1,"Vertical physical pixel mismatch");near(v.documentPoint(a,{640,480}),{40,60},"Mapping did not round trip");});
    test("viewport_fit_96_dip_margin_and_resize",[]{CanvasViewport v;v.resize({896,696},2,{Point{400,300}});near(v.zoom,4,"Fit physical zoom");near(v.pan,{},"Fit pan");require(v.followsFit,"Fit flag");v.resize({496,396},2,{Point{400,300}});near(v.zoom,2,"Fit failed on resize");near(v.origin({400,300}),{48,48},"Fit margin must be 48 each side");});
    test("viewport_anchor_zoom_and_clamp",[]{CanvasViewport v;v.resize({800,600},1.5);v.translate({20,-30});const Point anchor{103,201},size{640,480};auto doc=v.documentPoint(anchor,size);v.setZoom(5,anchor,size);near(v.documentPoint(anchor,size),doc,"Zoom anchor moved");require(!v.followsFit,"Manual zoom retained fit mode");v.setZoom(100,anchor,size);near(v.zoom,32,"Upper clamp");v.setZoom(0,anchor,size);near(v.zoom,.001,"Lower clamp");const auto before=v;v.setZoom(std::numeric_limits<double>::quiet_NaN(),anchor,size);near(v.zoom,before.zoom,"Nonfinite zoom changed state");near(v.pan,before.pan,"Nonfinite zoom changed pan");});
    test("viewport_dpr_change_preserves_center_document_point",[]{CanvasViewport v;const Point size{1000,800};v.resize({800,600},1);v.setZoom(2,{400,300},size);v.translate({70,-40});auto doc=v.documentPoint(v.center(),size);v.resize({1200,900},2,size);near(v.zoom,2,"DPR transition changed physical zoom");near(v.pan,{35,-20},"DPR transition did not scale pan");near(v.documentPoint(v.center(),size),doc,"DPR or window resize shifted center document point");});
    test("viewport_repeated_hand_motion_uses_current_pan",[]{CanvasViewport v;const Point size{600,400};v.resize({800,600},2);v.setZoom(1,{400,300},size);const Point pressView{170,120};const auto pressDoc=v.documentPoint(pressView,size);for(Point delta:std::array<Point,4>{{{10,0},{20,0},{30,7},{25,12}}}){auto doc=v.documentPoint({pressView.x+delta.x,pressView.y+delta.y},size);v.translate({(doc.x-pressDoc.x)*v.pointsPerPixel(),(doc.y-pressDoc.y)*v.pointsPerPixel()});near(v.pan,delta,"Hand accumulated or stalled under changing document coordinates");}require(!v.followsFit,"Pan retained fit mode");});
    test("viewport_empty_view_and_tiny_document_fit",[]{CanvasViewport v;v.zoom=3;v.fit({200,100});near(v.zoom,3,"Empty view changed zoom");v.resize({50,50},.5,Point{30000,30000});near(v.backingScale,1,"Backing scale minimum");near(v.zoom,.001,"Tiny fit zoom clamp");require(v.followsFit,"Fit flag lost");});
    std::cout<<"{\"suite\":\"canvas_viewport\",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"mac_differential\":false}\n";return failed?1:0;
}
