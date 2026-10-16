#pragma once
// A memory-only ring avoids adding file I/O to the game thread while profiling.
constexpr uint32_t PerfOffset=1929216,PerfCapacity=4096;
struct PerfRecord {
 uint32_t sequence,tick;
 float frameMs,padMs,bridgeMs,presentMs;
 uint32_t active,ready;
 float movementAge,cameraAge;
};
struct PerfHeader {uint32_t sequence,capacity;uint8_t padding[56];};
static_assert(sizeof(PerfRecord)==40&&PerfOffset+64+PerfCapacity*sizeof(PerfRecord)<SceneOffset);
LARGE_INTEGER perfFrequency{};
int64_t inputBegin=0,lastPresentTime=0;
float padTime=0,bridgeTime=0;
int64_t Counter(){LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
float Milliseconds(int64_t start,int64_t end){return perfFrequency.QuadPart?static_cast<float>((end-start)*1000.0/perfFrequency.QuadPart):0;}
void BeforeInput(){inputBegin=Counter();}
struct InputTimer {
 int64_t begin;
 InputTimer():begin(Counter()){padTime=inputBegin?Milliseconds(inputBegin,begin):0;}
 ~InputTimer(){bridgeTime=Milliseconds(begin,Counter());}
};
void RecordFrame(int64_t beforePresent,int64_t afterPresent){
 if(!shared)return;
 auto* header=reinterpret_cast<PerfHeader*>(shared+PerfOffset);
 uint32_t sequence=header->sequence+1;
 PerfRecord value{0,GetTickCount(),lastPresentTime?Milliseconds(lastPresentTime,afterPresent):0,padTime,bridgeTime,Milliseconds(beforePresent,afterPresent),active?1u:0u,guest.ready,static_cast<float>(GetTickCount()-guestMovement.tick),static_cast<float>(GetTickCount()-guestCamera.tick)};
 lastPresentTime=afterPresent;
 auto* slot=reinterpret_cast<PerfRecord*>(shared+PerfOffset+64)+(sequence%PerfCapacity);
 slot->sequence=0;MemoryBarrier();*slot=value;MemoryBarrier();slot->sequence=sequence;
 header->capacity=PerfCapacity;MemoryBarrier();header->sequence=sequence;
}
