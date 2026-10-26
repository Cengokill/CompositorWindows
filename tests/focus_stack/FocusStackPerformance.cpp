// Measurement: three 6000x4000 (24 MP) frames. CTest focus_stack.performance (label "performance").
// Fails when the known shifts are not recovered or the process peak exceeds the budget estimate.
#include "editing/FocusStack.h"
#include "FocusStackFrames.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <iostream>
using namespace compositor;
int main(){
    constexpr int W=6000,H=4000;
    std::vector<imaging::RgbaImage> frames;
    frames.push_back(focus_stack_fixture::frame(W,H,[](int x,int){return x>=W/3;}));
    frames.push_back(focus_stack_fixture::frame(W,H,[](int x,int){return x<W/3||x>=2*W/3;},4,-3));
    frames.push_back(focus_stack_fixture::frame(W,H,[](int x,int){return x<2*W/3;},-6,2));
    PROCESS_MEMORY_COUNTERS before{};GetProcessMemoryInfo(GetCurrentProcess(),&before,sizeof(before));
    const auto start=std::chrono::steady_clock::now();
    const auto result=editing::focusStack(frames);
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    PROCESS_MEMORY_COUNTERS after{};GetProcessMemoryInfo(GetCurrentProcess(),&after,sizeof(after));
    std::cout<<"frames=3 size="<<W<<"x"<<H<<" ms="<<ms
        <<" shifts="<<result.shifts[1].dx<<","<<result.shifts[1].dy<<" "<<result.shifts[2].dx<<","<<result.shifts[2].dy
        <<" inputs_mb="<<(before.WorkingSetSize>>20)<<" peak_working_set_mb="<<(after.PeakWorkingSetSize>>20)
        <<" budget_estimate_mb="<<(editing::focusStackWorkingBytes(W,H,3)>>20)<<'\n';
    const bool shifts=result.shifts[1]==editing::FocusShift{4,-3}&&result.shifts[2]==editing::FocusShift{-6,2};
    const bool withinBudget=after.PeakWorkingSetSize<=editing::focusStackWorkingBytes(W,H,3);
    if(!shifts)std::cerr<<"FAIL known shifts not recovered\n";
    if(!withinBudget)std::cerr<<"FAIL peak working set exceeds the budget estimate\n";
    return shifts&&withinBudget?0:1;
}
