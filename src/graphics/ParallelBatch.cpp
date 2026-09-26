#include "ParallelBatch.h"
#include <algorithm>
#include <atomic>
#include <cfenv>
#include <exception>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <windows.h>

namespace compositor::graphics {
namespace {
thread_local bool inParallelWorker=false;
class NativePool {
public:
    PTP_POOL pool{};TP_CALLBACK_ENVIRON environment{};
    const uint32_t workers=std::clamp<uint32_t>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),1,parallelBatchMaxWorkers);
    NativePool(){
        InitializeThreadpoolEnvironment(&environment);
        pool=CreateThreadpool(nullptr);
        if(!pool)throw std::system_error(int(GetLastError()),std::system_category(),"Create graphics CPU pool");
        SetThreadpoolThreadMaximum(pool,workers);
        if(!SetThreadpoolThreadMinimum(pool,workers)){
            const auto error=GetLastError();CloseThreadpool(pool);pool=nullptr;
            throw std::system_error(int(error),std::system_category(),"Start graphics CPU workers");
        }
        SetThreadpoolCallbackPool(&environment,pool);
    }
    ~NativePool(){CloseThreadpool(pool);DestroyThreadpoolEnvironment(&environment);}
};
struct Batch {
    size_t count;const std::function<void(size_t)>& callback;
    std::atomic<size_t> next{};std::atomic<bool> failed{};
    std::exception_ptr error;std::fenv_t environment{};
};
struct RestoreEnvironment {
    std::fenv_t previous{};const bool wasWorker=inParallelWorker;
    explicit RestoreEnvironment(const std::fenv_t& environment){
        if(std::fegetenv(&previous)||std::fesetenv(&environment))throw std::runtime_error("Cannot set graphics worker floating-point environment");
        inParallelWorker=true;
    }
    ~RestoreEnvironment(){inParallelWorker=wasWorker;(void)std::fesetenv(&previous);}
};
void CALLBACK consume(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK) noexcept {
    auto& batch=*static_cast<Batch*>(context);
    try{
        RestoreEnvironment environment(batch.environment);
        while(!batch.failed.load(std::memory_order_relaxed)){
            const auto index=batch.next.fetch_add(1,std::memory_order_relaxed);
            if(index>=batch.count)break;
            batch.callback(index);
        }
    }catch(...){if(!batch.failed.exchange(true))batch.error=std::current_exception();}
}
}
void runParallelBatch(size_t count,uint32_t workerLimit,const std::function<void(size_t)>& callback){
    if(workerLimit>parallelBatchMaxWorkers)throw std::invalid_argument("Graphics CPU worker limit must be0..16");
    if(!count)return;
    if(!callback)throw std::invalid_argument("Missing graphics batch callback");
    if(count>std::numeric_limits<size_t>::max()-parallelBatchMaxWorkers)throw std::length_error("Graphics batch count overflows worker cursor");
    if(inParallelWorker||workerLimit==1||count==1){for(size_t index=0;index<count;++index)callback(index);return;}
    static NativePool pool;
    const auto workers=std::min<size_t>(count,workerLimit?std::min(workerLimit,pool.workers):pool.workers);
    Batch batch{count,callback};
    if(std::fegetenv(&batch.environment))throw std::runtime_error("Cannot capture graphics floating-point environment");
    auto* work=CreateThreadpoolWork(consume,&batch,&pool.environment);
    if(!work)throw std::system_error(int(GetLastError()),std::system_category(),"Create graphics CPU batch");
    for(size_t i=0;i<workers;++i)SubmitThreadpoolWork(work);
    WaitForThreadpoolWorkCallbacks(work,FALSE);CloseThreadpoolWork(work);
    if(batch.error)std::rethrow_exception(batch.error);
}
}
