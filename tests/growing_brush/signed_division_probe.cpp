#include <array>
#include <cstdint>
#include <iostream>

int oldFloorDiv(int value,int divisor){int quotient=value/divisor;return value%divisor<0?quotient-1:quotient;}
int guardedFloorDiv(int value,int divisor){const int64_t v=value,d=divisor;return int(v>=0?v/d:-1-((-1-v)/d));}

int main(){
    constexpr std::array<int,8> inputs{-512,-257,-256,-255,-1,0,255,256};
    constexpr std::array<int,8> expected{-2,-2,-1,-1,-1,0,0,1};
    bool okay=true;
    for(size_t i=0;i<inputs.size();++i){
        volatile int runtimeValue=inputs[i],runtimeDivisor=256;
        const int old=oldFloorDiv(runtimeValue,runtimeDivisor),guarded=guardedFloorDiv(runtimeValue,runtimeDivisor);
        std::cout<<"value="<<inputs[i]<<" expected="<<expected[i]<<" old="<<old<<" guarded="<<guarded<<'\n';
        okay=okay&&old==expected[i]&&guarded==expected[i];
    }
    volatile int runtimeX=-40,runtimePhase=216;
    const int x=runtimeX,phase=runtimePhase,oldKey=oldFloorDiv(x-phase,256),guardedKey=guardedFloorDiv(x-phase,256);
    const int oldLocal=x-(oldKey*256+phase),guardedLocal=x-(guardedKey*256+phase);
    std::cout<<"site x="<<x<<" phase="<<phase<<" delta="<<x-phase<<" expected_key=-1 old_key="<<oldKey<<" guarded_key="<<guardedKey<<" expected_local=0 old_local="<<oldLocal<<" guarded_local="<<guardedLocal<<'\n';
    return okay&&oldKey==-1&&guardedKey==-1&&oldLocal==0&&guardedLocal==0?0:1;
}
