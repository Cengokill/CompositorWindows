#include "editing_transform/TransformGeometry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace compositor;
namespace t=compositor::editing_transform;
namespace {
int passed=0,failed=0;
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void near(double a,double b,const char* text,double tolerance=1e-8){require(std::abs(a-b)<=tolerance,text);}
void near(Point a,Point b,const char* text,double tolerance=1e-8){near(a.x,b.x,text,tolerance);near(a.y,b.y,text,tolerance);}
template<class F>void test(const char* name,F body){try{body();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
Point plus(Point a,Point b){return {a.x+b.x,a.y+b.y};}
Transform value(double x=31,double y=-19,double width=200,double height=100,double angle=37){Transform out;out.x=x;out.y=y;out.width=width;out.height=height;out.rotation=angle;return out;}
}
int main(){
    test("source_rotated_anchor_every_handle_ratio_modifier_and_flip",[]{
        for(bool flip:{false,true})for(bool lock:{false,true})for(bool shift:{false,true})for(bool alt:{false,true})for(int i=0;i<8;++i){
            auto original=value();original.flipX=flip;original.flipY=!flip;
            const auto handle=t::handleUnits[static_cast<size_t>(i)];const Point anchor=alt?Point{.5,.5}:Point{1-handle.x,1-handle.y};
            const auto start=t::outlinePoint(original,handle);t::Drag drag{original,start,{t::ModeKind::Resize,i},{}};
            const auto changed=drag.updated(plus(start,{34,17}),lock,{shift,alt});
            require(changed.valid(),"Invalid rotated resize");near(t::outlinePoint(original,anchor),t::outlinePoint(changed,anchor),"Anchor moved");
            if(lock!=shift)near(changed.width/changed.height,2,"Aspect lock XOR Shift");
            require(changed.flipX==original.flipX&&changed.flipY==original.flipY,"Resize changed flips");
        }
    });
    test("grab_offset_no_jump_and_projection_ratio",[]{
        auto original=value(0,0,100,50,0);const Point start{104,54};t::Drag drag{original,start,{t::ModeKind::Resize,4},{}};
        require(drag.updated(start,false)==original,"Offset grab jumped");
        auto changed=drag.updated({154,54},true);near(changed.width,140,"Diagonal projection width");near(changed.height,70,"Diagonal projection height");
        auto free=drag.updated({154,54},true,{true});near(free.width,150,"Shift width");near(free.height,50,"Shift height");
        auto centered=drag.updated({114,59},false,{false,true});near(centered.width,120,"Centered width");near(centered.height,60,"Centered height");near(t::center(centered),t::center(original),"Centered resize moved center");
    });
    test("source_move_rotate_shift_constraints",[]{
        const auto original=value(0,0,100,50,0);t::Drag move{original,{40,20},{t::ModeKind::Move},{}};
        near(Point{move.updated({60,25},true,{true}).x,move.updated({60,25},true,{true}).y},{20,0},"Shift axis move");
        auto tie=move.updated({50,30},false,{true});near(Point{tie.x,tie.y},{10,0},"Shift axis tie");
        t::Drag rotate{original,{100,25},{t::ModeKind::Rotate},{}};const auto rotated=rotate.updated({50,75},true);
        near(rotated.rotation,90,"Clockwise rotation");near(t::center(rotated),t::center(original),"Rotate moved center");
        near(rotate.updated({99,45},true,{true}).rotation,15,"Rotation15degree quantization");
        rotate.original.rotation=720;near(rotate.updated({50,75},false).rotation,810,"Rotation turn continuity");
    });
    test("resize_crossing_clamps_one_without_implicit_flip",[]{
        auto original=value(0,0,100,50,0);t::Drag drag{original,{100,50},{t::ModeKind::Resize,4},{}};
        const auto crossed=drag.updated({-100,-100},false);near(crossed.width,1,"Crossed width clamp");near(crossed.height,1,"Crossed height clamp");require(!crossed.flipX&&!crossed.flipY,"Crossing flipped image");
        near(t::outlinePoint(crossed,{0,0}),{0,0},"Crossing moved anchor");
    });
    test("whole_rounding_away_from_zero_and_fractional_typed_fields",[]{
        auto original=value(-1.5,2.5,100.4,50.6,-14.5);auto rounded=t::roundedTransform(original);
        near(rounded.x,-2,"Negative half rounding");near(rounded.y,3,"Positive half rounding");near(rounded.width,100,"Width rounding");near(rounded.height,51,"Height rounding");near(rounded.rotation,-15,"Degree rounding");
        const auto field=t::resizedField(original,201.25,true,true);near(field.width,201.25,"Typed fraction lost");near(field.height,50.6*201.25/100.4,"Typed locked height");near(Point{field.x,field.y},{original.x,original.y},"Inspector resize must keep origin");
    });
    test("scale_percent_local_flip_and_world_mirror_differ",[]{
        auto original=value(30,40,200,300,30);original.sampling=Transform::Sampling::Nearest;
        near(t::scalePercent(original,{100,100}),200,"Scale width reference");const auto scaled=t::scaledPercent(original,50,{100,100});near(scaled.width,50,"Scale width");near(scaled.height,50,"Scale height");near(t::center(scaled),t::center(original),"Scale center");require(scaled.sampling==original.sampling,"Sampling lost");
        auto local=t::flippedLocal(original,true);near(local.rotation,30,"Inspector flip changes angle");near(t::center(local),t::center(original),"Inspector flip moved");
        for(bool horizontal:{false,true}){auto mirrored=t::mirrored(original,horizontal,400);near(mirrored.rotation,-30,"World mirror negates angle");
            for(Point p:std::array<Point,3>{{{0,0},{.3,.7},{1,1}}}){auto expected=original.fromUnit(p);if(horizontal)expected.x=800-expected.x;else expected.y=800-expected.y;near(mirrored.fromUnit(p),expected,"World mirror point map");}}
    });
    test("outline_flips_mapping_and_fixed_DIP_stem",[]{
        auto original=value(100,200,100,50,90);const t::ViewMapping view{1.5,{57,-30}};
        auto geometry=t::OverlayGeometry::fromTransform(original,view);
        for(size_t i=0;i<8;++i)near(view.toDocument(geometry.handles[i]),t::outlinePoint(original,t::handleUnits[i]),"Document/view mapping");
        near(std::hypot(geometry.rotationHandle.x-geometry.handles[1].x,geometry.rotationHandle.y-geometry.handles[1].y),28,"Stem must be DIP fixed");
        original.flipX=true;original.flipY=true;auto flipped=t::OverlayGeometry::fromTransform(original,view);require(flipped.handles==geometry.handles,"Flips swapped outline handles");
        require(t::contains(original,t::center(original))&&t::contains(original,{150,265})&&!t::contains(original,{190,225}),"Rotated bounds hit");
    });
    test("source_rotated_edges_corners_rotation_hit_and_cursor",[]{
        const auto original=value(100,100,400,300,37);const auto g=t::OverlayGeometry::fromTransform(original,{});
        for(int i=0;i<8;++i)require(g.hit(g.handles[static_cast<size_t>(i)])==t::Mode{t::ModeKind::Resize,i},"Handle hit");
        for(size_t side=0;side<4;++side){const auto a=g.handles[side*2],b=g.handles[(side*2+2)%8];require(g.hit({a.x*.75+b.x*.25,a.y*.75+b.y*.25})==t::Mode{t::ModeKind::Resize,static_cast<int>(side*2+1)},"Rotated edge hit");}
        require(g.hit(g.rotationHandle)==t::Mode{t::ModeKind::Rotate},"Rotation hit");require(!g.hit(t::center(original)),"Body should not count as handle");
        auto zero=t::OverlayGeometry::fromTransform(value(0,0,300,100,0),{});require(zero.resizeCursor(0)==t::Cursor::DiagonalDown&&zero.resizeCursor(1)==t::Cursor::Vertical&&zero.resizeCursor(3)==t::Cursor::Horizontal,"Cursor directions");
        require(zero.cursor({t::ModeKind::Resize,0},{false,false,true})==t::Cursor::Distort,"Command distortion cursor");require(zero.cursor({t::ModeKind::Move},{false,true})==t::Cursor::Duplicate,"Option duplicate cursor");
    });
    test("snap_smallest_each_axis_fixed_ties_and_threshold",[]{
        t::SnapTargets targets{{0,9,91},{20,49,100}};auto snap=t::snapOffset({10,10,80,80},targets,10);
        near(snap.offset,{-1,-1},"Closest independent snap");require(snap.x==9&&snap.y==49,"Snap guides");
        t::SnapTargets ties{{8,12},{}};auto tie=t::snapOffset({10,0,80,80},ties,2);near(tie.offset.x,-2,"First equal-distance target must win");
        require(!t::snapOffset({10,0,80,80},ties,1.99).x,"Snap threshold relaxed");
    });
    test("preview_rounds_before_snap_and_control_disables_only_snap",[]{
        t::Drag drag{value(0,0,100,50,0),{0,0},{t::ModeKind::Move},{}};t::SnapTargets targets{{15},{}};
        auto snap=t::previewDrag(drag,{9.5,0},false,{},targets,2);near(snap.transform.x,15,"Round10 then snap15 at5pixel tolerance");require(snap.snap.x==15,"Missing snapped guide");
        auto free=t::previewDrag(drag,{9.5,0},false,{false,false,false,true},targets,2);near(free.transform.x,10,"Control must keep source rounding");require(!free.snap.x,"Control snapped");
        auto tooFar=t::previewDrag(drag,{9.5,0},false,{},targets,4);near(tooFar.transform.x,10,"Zoom must shrink document tolerance");
        drag.mode={t::ModeKind::Resize,4};drag.start={100,50};auto resized=t::previewDrag(drag,{109.5,50},false,{},targets,1);require(!resized.snap.x,"Resize should not move snap");
    });
    test("visible_hierarchy_snap_bounds_exclusion_and_displayed_draft",[]{
        Document doc;doc.width=400;doc.height=300;auto raster=Raster::filled(1,1);
        Layer hidden;hidden.id="hidden";hidden.group=true;hidden.visible=false;
        Layer child;child.id="child";child.parentId="hidden";child.raster=raster;child.transform=value();
        Layer group;group.id="folder";group.group=true;
        Layer nested; nested.id="nested";nested.parentId="folder";nested.raster=raster;nested.transform=value(100,120,100,60,90);
        Layer last;last.id="last";last.raster=raster;last.transform=value(10,10,10,10,0);
        doc.layers={child,hidden,nested,group,last};auto layers=t::visiblePlacements(doc);
        require(layers.size()==2&&layers[0].id=="nested"&&layers[1].id=="last","Visible hierarchy/source order");
        const std::array<std::string,1> excluded{"last"};auto targets=t::collectSnapTargets({400,300},layers,excluded);
        require(targets.xs==std::vector<double>{0,200,400,120,150,180},"Rotated snap upright xbounds");require(targets.ys==std::vector<double>{0,150,300,100,150,200},"Rotated snap upright ybounds");
        auto draft=t::visiblePlacements(doc,[](const Layer& layer){auto transformed=layer.transform;transformed.x+=20;return transformed;});near(draft[0].transform.x,120,"Displayed transform callback");
    });
    test("source_press_outside_active_moves_option_delays_duplicate",[]{
        const auto original=value(150,100,100,100,0);std::vector<t::Placement> layers{{"active",original}};t::PressContext context;context.activeId="active";context.activeTransform=original;context.visibleLayers=layers;
        auto press=t::resolvePress(context,{20,20},{},{},{false,true});require(press&&press->layerId=="active"&&!press->picked&&press->duplicateOnFirstDrag&&press->mode.kind==t::ModeKind::Move,"Option outside layer must move duplicate on first drag");
        t::Drag drag{original,{20,20},press->mode,{}};const auto moved=drag.updated({40,30},false);near(Point{moved.x,moved.y},{170,110},"Source TransformPressTests position");
    });
    test("source_autoselect_command_active_priority_and_pending_edit",[]{
        std::vector<t::Placement> layers{{"first",value(0,0,100,100,0)},{"second",value(250,0,100,100,0)}};
        t::PressContext c;c.activeId="first";c.activeTransform=layers[0].transform;c.visibleLayers=layers;
        auto disabled=t::resolvePress(c,{300,50},{},{},{});require(disabled&&disabled->layerId=="first"&&!disabled->picked,"Auto select default off");
        c.autoSelect=true;auto automatic=t::resolvePress(c,{300,50},{},{},{});require(automatic&&automatic->layerId=="second"&&automatic->picked,"Auto select outside active");
        c.autoSelect=false;auto command=t::resolvePress(c,{300,50},{},{},{false,false,true});require(command&&command->layerId=="second"&&command->picked,"Command override pick");
        c.hasEdit=true;auto pending=t::resolvePress(c,{300,50},{},{},{false,false,true});require(pending&&pending->layerId=="first"&&!pending->picked,"Pending transform must not pick");
        c.hasEdit=false;c.autoSelect=true;layers[1].transform=value(0,0,100,100,0);auto inside=t::resolvePress(c,{50,50},{},{},{});require(inside&&inside->layerId=="first"&&!inside->picked,"Inside active wins auto select");
        auto override=t::resolvePress(c,{50,50},{},{},{false,false,true});require(override&&override->layerId=="second"&&override->picked,"Command picks topmost by bounds even transparent pixels");
    });
    test("hidden_controls_persistent_override_and_group_press",[]{
        auto original=value(150,100,100,100,0);auto overlay=t::OverlayGeometry::fromTransform(original,{});t::PressContext c;c.activeId="active";c.activeTransform=original;c.controlsVisible=false;
        auto hidden=t::resolvePress(c,overlay.handles[0],{},overlay,{false,false,true});require(hidden&&hidden->mode.kind==t::ModeKind::Move,"Hidden controls must not distort");
        c.persistent=true;auto visible=t::resolvePress(c,overlay.handles[0],{},overlay,{false,false,true});require(visible&&visible->mode==t::Mode{t::ModeKind::Distort,0},"Persistent controls/Command distortion");
        auto resize=t::resolvePress(c,overlay.handles[0],{},overlay,{false,true});require(resize&&resize->mode.kind==t::ModeKind::Resize&&!resize->duplicateOnFirstDrag,"Option handle must center resize without duplication");
        c.persistent=false;c.activeTransform.reset();c.groupBox=original;auto group=t::resolvePress(c,{0,0},{},{},{});require(group&&group->layerId=="active","Empty canvas moves selected group");
    });
    test("distort_corner_edge_axis_shift_and_convex_guard",[]{
        const auto original=value(0,0,100,80,0);const auto c=t::corners(original);t::Drag drag{original,{0,0},{t::ModeKind::Distort,0},c};
        auto corner=*drag.movedCorners({20,10},true);near(corner[0],{20,0},"Corner Shift axis");near(corner[1],c[1],"Corner changed neighbor");
        drag.mode={t::ModeKind::Distort,7};auto edge=*drag.movedCorners({-10,20});near(edge[3],{-10,100},"Edge start");near(edge[0],{-10,20},"Edge wrap endpoint");near(edge[1],c[1],"Edge changed opposite");
        drag.mode={t::ModeKind::Move};auto moved=*drag.movedCorners({3,4});for(size_t i=0;i<4;++i)near(moved[i],plus(c[i],{3,4}),"Distorted body move");
        require(t::usableCorners(c),"Valid corners refused");auto bow=c;std::swap(bow[1],bow[2]);require(!t::usableCorners(bow),"Bow tie accepted");
        drag.mode={t::ModeKind::Distort,0};auto invalid=t::previewDrag(drag,{200,200},false,{}, {},1);require(!invalid.accepted,"Host must retain last valid distortion");
        auto g=t::OverlayGeometry::fromCorners(c,{2,{10,20}});require(!g.showsRotation&&g.rotationHandle==g.handles[1],"Distortion shows rotate handle");near(g.handles[7],{10,100},"Distortion midpoint");
    });
    test("group_following_exact_move_uniform_rotation_and_flips",[]{
        const auto placement=value(20,30,40,20,397);auto old=value(0,0,100,100,0);auto moved=old;moved.x=13.25;moved.y=-8.5;
        auto carried=t::following(placement,old,moved);near(Point{carried.x,carried.y},{33.25,21.5},"Exact group move");near(carried.rotation,397,"Move rotation changed");
        auto changed=value(50,70,200,200,30);changed.flipX=true;
        for(bool flip:{false,true}){auto member=placement;member.flipX=flip;const auto result=t::following(member,old,changed);
            for(Point p:std::array<Point,4>{{{0,0},{1,0},{.3,.7},{1,1}}})near(result.fromUnit(p),changed.fromUnit(old.toUnit(member.fromUnit(p))),"Affine group follow",1e-7);
            require(result.flipX==member.flipX,"Horizontal flip representation changed");require(std::abs(result.rotation-member.rotation)<=180,"Closest rotation lost");}
    });
    test("uneven_group_scale_drops_shear_source_decomposition",[]{
        auto member=value(10,20,30,20,765);const auto old=value(0,0,100,100,0),changed=value(0,0,200,100,0);
        const auto result=t::following(member,old,changed);near(result.width,30*std::sqrt(2.5),"Shear projected width");near(result.height,1200/result.width,"Shear projected height");
        near(result.rotation,720+std::atan2(1.,2.)*180/3.14159265358979323846,"Closest decomposed rotation");
        near(t::center(result),changed.fromUnit(old.toUnit(t::center(member))),"Group center mapping");
        auto differentSampling=result;differentSampling.sampling=Transform::Sampling::Nearest;require(t::samePlacement(result,differentSampling),"Sampling alters placement equality");
    });
    test("invalid_numeric_input_and_handle_bounds",[]{
        const auto original=value();t::Drag drag{original,{0,0},{t::ModeKind::Move},{}};
        require(drag.updated({std::numeric_limits<double>::infinity(),0},false)==original,"Nonfinite drag changed placement");require(drag.updated({2000000,0},false)==original,"Origin budget not enforced");
        require(t::scaledPercent(original,-1,{100,100})==original,"Invalid percent accepted");require(t::resizedField(original,300001,true,false)==original,"Oversized field accepted");
        bool rejected=false;try{drag.mode={t::ModeKind::Resize,8};(void)drag.updated({1,1},false);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid handle not rejected");
        rejected=false;try{(void)t::ViewMapping{0,{}}.toDocument({0,0});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid view scale accepted");
    });
    std::cout<<"Transform geometry: "<<passed<<" passed, "<<failed<<" failed; native analytic/source assertions, Mac differential unavailable\n";
    return failed?1:0;
}
