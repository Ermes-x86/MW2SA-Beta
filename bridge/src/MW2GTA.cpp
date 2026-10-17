#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <string>
#include <vector>
#include "plugin.h"
#include "common.h"
#include "CCamera.h"
#include "CDraw.h"
#include "CPlayerPed.h"
#include "CWorld.h"
#include "CPlayerInfo.h"
#include "CColModel.h"
#include "CCollisionData.h"
#include "CWeapon.h"
#include "CWeaponInfo.h"
#include "CVehicle.h"
#include "CObject.h"
#include "CPad.h"
#include "CTimer.h"
#include "CMenuManager.h"
#include "CFont.h"
#include "CPools.h"
#include "CStreaming.h"
#include "CPopulation.h"
#include "CCutsceneMgr.h"
#include "CWanted.h"
#include "CExplosion.h"
#include "CPedIntelligence.h"
#include "CTaskSimpleStandStill.h"
#include "FxManager_c.h"
#include "FxSystem_c.h"
#include "CHud.h"
#include "Protocol.h"

namespace {
using namespace mw2gta;
constexpr float Pi=3.14159265358979323846f;
HANDLE mapping=nullptr,controlMutex=nullptr,frameMutex=nullptr;
uint8_t* shared=nullptr;
PROCESS_INFORMATION child{};
Guest guest{};
GuestCamera guestCamera{};
Features featureState{};
std::vector<CatalogEntry> weaponCatalog;
std::vector<CVector> grenadeCollisionCenters;
bool weaponMenu=false;size_t selectedWeapon=0;
bool menuAcceptHeld=false;
DWORD hudFixtureUntil=0;char* hudFixtureOriginalZone=nullptr;
uint32_t inventoryRequest=0,inventoryKind=0,inventoryWeapon=0,lastExplosion=0;
DWORD inventoryNoticeUntil=0;std::string inventoryNotice;
float mouseTotalX=0,mouseTotalY=0;
Host host{};
bool requested=false,active=false,settings=false,previousKeys[256]{};
CPed* controlled=nullptr;
bool previousVisible=true;
int previousWeapon=0;
std::array<char,7> previousControlBlocks{};
void SaveControlBlocks(CPad* pad){previousControlBlocks={pad->bDisablePlayerEnterCar,pad->bDisablePlayerDuck,pad->bDisablePlayerFireWeapon,pad->bDisablePlayerFireWeaponWithL1,pad->bDisablePlayerCycleWeapon,pad->bDisablePlayerJump,pad->bDisablePlayerDisplayVitalStats};}
void RestoreControlBlocks(CPad* pad){pad->bDisablePlayerEnterCar=previousControlBlocks[0];pad->bDisablePlayerDuck=previousControlBlocks[1];pad->bDisablePlayerFireWeapon=previousControlBlocks[2];pad->bDisablePlayerFireWeaponWithL1=previousControlBlocks[3];pad->bDisablePlayerCycleWeapon=previousControlBlocks[4];pad->bDisablePlayerJump=previousControlBlocks[5];pad->bDisablePlayerDisplayVitalStats=previousControlBlocks[6];}
float yaw=0,pitch=0,fov=80,eyeHeight=0.72f,sensitivity=0.13f,uiX=0,uiY=0;
DWORD lastTime=0,lastReport=0,runtimeStarted=0,unreadySince=0; bool runtimeWasReady=false; std::string runtimeError;
uint32_t lastShot=0,lastFrame=0,hits=0,received=0;
bool selfTest=false,testDone=false; DWORD testStarted=0; int testTarget=0; CVector testStart; float testDistance=0;
uintptr_t cameraOriginal=0;
uintptr_t districtPrintOriginal=0;
std::wstring runtimeRoot,iniPath,bridgeName=L"MW2GTA_v1",runtimeExecutable=L"iw4l-gta.exe";
FILE* logFile=nullptr;
IDirect3DTexture9* texture=nullptr;
std::vector<uint8_t> pixels;
uint32_t imageWidth=0,imageHeight=0;
uint32_t renderedFrames=0,uploadedFrames=0,lastFpsTime=0;

void Log(const char* text){if(logFile){fprintf(logFile,"%lu %s\n",GetTickCount(),text);fflush(logFile);}}
struct Lock {HANDLE handle; bool acquired; explicit Lock(HANDLE h):handle(h),acquired(false){if(h){DWORD s=WaitForSingleObject(h,0);acquired=s==WAIT_OBJECT_0||s==WAIT_ABANDONED;}} ~Lock(){if(acquired)ReleaseMutex(handle);}};
#include "MovementHost.h"
#include "GraphicsHost.h"
#include "PerformanceHost.h"

bool Down(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}
bool Edge(int key){bool d=Down(key),result=d&&!previousKeys[key];previousKeys[key]=d;return result;}
bool Focused(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
void RestoreWorldReaction();
void ClearGrenades();
void RestoreInventorySafety();
void ReleaseTexture(){if(texture){texture->Release();texture=nullptr;}lastFrame=0;}
// A cutscene or mission camera owns TheCamera when it suspends the mod;
// jump-cutting back to CJ would cancel the scene it just started.
void Restore(bool restoreCamera=true){
 RestoreInventorySafety();
 if(active){RestoreWorldReaction();ClearGrenades();weaponMenu=false;settings=false;}
 if(controlled && controlled==FindPlayerPed()){
   controlled->bIsVisible=previousVisible;
   controlled->SetCurrentWeapon(previousWeapon);
   RestoreMovement(controlled);
 }
 if(active){RestoreControlBlocks(CPad::GetPad(0));if(restoreCamera)TheCamera.RestoreWithJumpCut();}
 controlled=nullptr;active=false;
}
float ReadFloat(const wchar_t* key,float fallback){wchar_t value[64],def[64];swprintf_s(def,L"%.4f",fallback);GetPrivateProfileStringW(L"Bridge",key,def,value,64,iniPath.c_str());return static_cast<float>(_wtof(value));}
void SaveFov(){wchar_t value[32];swprintf_s(value,L"%.1f",fov);WritePrivateProfileStringW(L"Bridge",L"FOV",value,iniPath.c_str());}
bool CreateBridge(){
 mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,MappingSize,(L"Local\\"+bridgeName).c_str());
 if(!mapping)return false;
 bool existing=GetLastError()==ERROR_ALREADY_EXISTS;
 shared=static_cast<uint8_t*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,MappingSize));
 controlMutex=CreateMutexW(nullptr,FALSE,(L"Local\\"+bridgeName+L"_control").c_str());
 frameMutex=CreateMutexW(nullptr,FALSE,(L"Local\\"+bridgeName+L"_frame").c_str());
 sceneMutex=CreateMutexW(nullptr,FALSE,(L"Local\\"+bridgeName+L"_scene").c_str());
 if(!shared||!controlMutex||!frameMutex||!sceneMutex)return false;
 Lock lock(controlMutex);if(!lock.acquired)return false;
 if(!existing)ZeroMemory(shared,MappingSize);
 auto* c=reinterpret_cast<Control*>(shared);c->magic=Magic;c->version=Version;c->host.tick=GetTickCount();c->host.shutdown=0;
 reinterpret_cast<Features*>(shared+FeaturesOffset)->reserved[4]=GetCurrentProcessId();
 return true;
}
// MW2 render size. "Match GTA" keeps GTA's aspect ratio at the largest size
// the shared image holds (1280x720), so the overlay lines up with GTA's view.
constexpr int ResolutionChoices=5;
int resolutionChoice=2;
const char* ResolutionLabels[ResolutionChoices]={"Match GTA","640x480","800x600","1024x576","1280x720"};
void ResolutionFor(int choice,uint32_t& width,uint32_t& height){
 static const uint32_t fixed[ResolutionChoices][2]={{0,0},{640,480},{800,600},{1024,576},{1280,720}};
 if(choice>0){width=fixed[choice][0];height=fixed[choice][1];return;}
 float w=static_cast<float>(std::max(RsGlobal.maximumWidth,320)),h=static_cast<float>(std::max(RsGlobal.maximumHeight,240));
 float scale=std::min({1.0f,MaxWidth/w,MaxHeight/h});
 width=static_cast<uint32_t>(w*scale)&~1u;height=static_cast<uint32_t>(h*scale)&~1u;
}
// Rewrites only the resolution line of the guest settings; every other
// player preference in the file is preserved.
void WriteGuestResolution(){
 uint32_t width=0,height=0;ResolutionFor(resolutionChoice,width,height);
 std::wstring path=runtimeRoot+L"\\gta-bridge-settings.cfg";
 FILE* in=_wfopen(path.c_str(),L"rb");if(!in)return;
 std::string text,line;char buffer[4096];size_t n;while((n=fread(buffer,1,sizeof(buffer),in))>0)text.append(buffer,n);fclose(in);
 std::string wanted="resolution="+std::to_string(width)+"x"+std::to_string(height);
 size_t at=text.find("resolution=");
 if(at==std::string::npos)return;
 size_t end=text.find_first_of("\r\n",at);if(end==std::string::npos)end=text.size();
 if(text.compare(at,end-at,wanted)==0)return;
 text.replace(at,end-at,wanted);
 FILE* out=_wfopen(path.c_str(),L"wb");if(!out)return;fwrite(text.data(),1,text.size(),out);fclose(out);
 Log(("MW2 resolution set: "+wanted).c_str());
}
// Restarting asks the helper to exit through its normal shutdown path (it
// flushes its account save), then starts it again once the process is gone.
bool runtimeRestartPending=false;DWORD runtimeRestartStarted=0;
void RequestRuntimeRestart(){
 if(runtimeRestartPending||!child.hProcess)return;
 runtimeRestartPending=true;runtimeRestartStarted=GetTickCount();requested=false;host.shutdown=1;Restore();
 Log("MW2 runtime restart requested");
}
bool StartRuntime();
void UpdateRuntimeRestart(){
 if(!runtimeRestartPending)return;
 bool exited=!child.hProcess||WaitForSingleObject(child.hProcess,0)!=WAIT_TIMEOUT;
 if(!exited&&GetTickCount()-runtimeRestartStarted>10000){TerminateProcess(child.hProcess,1);Log("MW2 runtime did not exit; terminated for restart");return;}
 if(!exited)return;
 runtimeRestartPending=false;host.shutdown=0;requested=StartRuntime();
}
bool StartRuntime(){
 if(child.hProcess){if(WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT)return true;CloseHandle(child.hProcess);CloseHandle(child.hThread);child={};}
 std::wstring exe=runtimeRoot+L"\\"+runtimeExecutable;
 if(GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES){Log("Missing adapted iw4l-gta.exe");return false;}
 WriteGuestResolution();
 SetEnvironmentVariableW(L"IW4L_GTA_BRIDGE",bridgeName.c_str());
 SetEnvironmentVariableW(L"WGPU_BACKEND",L"dx12");
 SetEnvironmentVariableW(L"IW4L_PRESENT_MODE",L"AutoNoVsync");
 SetEnvironmentVariableW(L"IW4L_SETTINGS_PATH",(runtimeRoot+L"\\gta-bridge-settings.cfg").c_str());
 SetEnvironmentVariableW(L"IW4L_PIPELINED_RENDERING",L"0");
 SetEnvironmentVariableW(L"IW4L_SCRIPT_DVARS",L"set scr_dm_timelimit 0; set scr_dm_scorelimit 0; set scr_war_timelimit 0; set scr_war_scorelimit 0");
 SetEnvironmentVariableW(L"IW4L_IMAGE_DECODE_BUDGET_MIB",L"128");
 std::wstring command=L"\""+exe+L"\" --cmds \"wait progression; unlock all; map mp_rust; wait world; spawn 0; force_match_start\" menu";
 STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
 bool okay=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,runtimeRoot.c_str(),&startup,&child)!=0;
 SetEnvironmentVariableW(L"IW4L_GTA_BRIDGE",nullptr);
  if(okay){runtimeStarted=GetTickCount();unreadySince=0;runtimeWasReady=false;runtimeError.clear();guest={};lastShot=0;ReleaseTexture();Lock lock(controlMutex);if(lock.acquired){reinterpret_cast<Control*>(shared)->guest={};ZeroMemory(shared+FeaturesOffset,MappingSize-FeaturesOffset);reinterpret_cast<Features*>(shared+FeaturesOffset)->reserved[4]=GetCurrentProcessId();inventoryRequest=inventoryKind=inventoryWeapon=lastExplosion=0;featureState={};weaponCatalog.clear();}}
 else{char message[120];sprintf_s(message,"MW2 start failed (%lu). F6 retry",GetLastError());runtimeError=message;}
 Log(okay?"Started real IW4L weapon runtime":runtimeError.c_str());return okay;
}
void WatchRuntime(){
 if(!requested||!child.hProcess)return;
 DWORD code=0;if(GetExitCodeProcess(child.hProcess,&code)&&code!=STILL_ACTIVE){char message[140];sprintf_s(message,"MW2 runtime stopped (%lu). F6 retry",code);runtimeError=message;requested=false;Restore();Log(message);return;}
 DWORD now=GetTickCount();
 if(guest.ready&&now-guest.tick<1500){runtimeWasReady=true;unreadySince=0;return;}
 if(!unreadySince)unreadySince=now;
 if(now-unreadySince<(runtimeWasReady?15000u:180000u))return;
 runtimeError=runtimeWasReady?"MW2 connection lost. F6 retry":"MW2 startup timed out. F6 retry";
 Log(runtimeError.c_str());TerminateProcess(child.hProcess,1);requested=false;Restore();
}
BOOL CALLBACK MoveGuestWindow(HWND window,LPARAM parameter){DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==static_cast<DWORD>(parameter)&&GetWindow(window,GW_OWNER)==nullptr){RECT bounds{};GetWindowRect(window,&bounds);if(bounds.left>-9000){SetWindowPos(window,HWND_BOTTOM,-10000,0,800,600,SWP_NOACTIVATE|SWP_NOSIZE);ShowWindow(window,SW_SHOWNOACTIVATE);}return FALSE;}return TRUE;}
CVector Eye(CPed* ped){
 if(active&&appliedMovement&&guestMovement.epoch==hostMovement.epoch){auto foot=PlayerFoot(ped);foot.z+=guestMovement.viewHeight;return foot;}
 CVector p=ped->GetPosition();p.z+=eyeHeight;return p;
}
#include "GameplayHost.h"
void ConsumeShot(const Shot& shot,CPed* player){
 received++;
 if(!active || shot.sequence==0 || GetTickCount()-shot.hostTick>500 || !std::isfinite(shot.damage)||shot.damage<=0)return;
 CVector origin(shot.origin[0],shot.origin[1],shot.origin[2]);
 CVector direction(shot.direction[0],shot.direction[1],shot.direction[2]);
 float length=direction.Magnitude();if(!std::isfinite(length)||length<0.5f||length>1.5f)return;direction/=length;
 const float range=std::clamp(shot.range,1.0f,2000.0f);CVector end=origin+direction*range;
 // Melee swings arrive as short emissions: silent, and resolved against the
 // closest ped in front when the centre ray misses, like IW4's melee width.
 const bool melee=range<3.0f,assist=melee&&shot.reserved[0]==0;
 if(!melee)NotifyGunshot(player,origin,end);
 else{
  CColPoint probe{};CEntity* first=nullptr;
  CEntity* saved=CWorld::pIgnoreEntity;CWorld::pIgnoreEntity=player;
  bool direct=CWorld::ProcessLineOfSight(origin,end,probe,first,false,true,true,true,false,false,false,false)&&first&&first->m_nType==ENTITY_TYPE_PED;
  CWorld::pIgnoreEntity=saved;
  if(!direct&&assist){
   std::array<CEntity*,16> nearby{};short n=0;CPed* best=nullptr;float bestDot=std::cos(35.0f*Pi/180);
   CWorld::FindObjectsInRange(origin,2.2f,false,&n,static_cast<short>(nearby.size()),nearby.data(),false,false,true,false,false);
   for(short i=0;i<n;i++){
    auto* ped=static_cast<CPed*>(nearby[i]);if(!ped||ped==player||ped->m_fHealth<=0)continue;
    auto delta=ped->GetPosition()-origin;float d=delta.Magnitude();if(d<.01f)continue;
    float dot=(delta.x*direction.x+delta.y*direction.y+delta.z*direction.z)/d;
    if(dot>bestDot&&CWorld::GetIsLineOfSightClear(origin,ped->GetPosition(),true,false,false,true,false,false,false)){best=ped;bestDot=dot;}
   }
   if(best){direction=best->GetPosition()-origin;direction/=direction.Magnitude();end=origin+direction*2.5f;}
  }
 }
 CColPoint point{};CEntity* entity=nullptr;
 CEntity* ignored=CWorld::pIgnoreEntity;CWorld::pIgnoreEntity=player;
 bool hit=CWorld::ProcessLineOfSight(origin,end,point,entity,true,true,true,true,true,false,false,false);
 CWorld::pIgnoreEntity=ignored;
 if(!hit||!entity)return;
 float distance=(point.m_vecPoint-origin).Magnitude();
 float fraction=shot.minDamageRange>shot.maxDamageRange?std::clamp((distance-shot.maxDamageRange)/(shot.minDamageRange-shot.maxDamageRange),0.0f,1.0f):0.0f;
 int damage=std::max(1,static_cast<int>(std::lround(shot.damage+(shot.minDamage-shot.damage)*fraction)));
 if(entity->m_nType==ENTITY_TYPE_PED && entity!=player){
   auto* victim=static_cast<CPed*>(entity);
   if(victim->m_fHealth<=0)return;

   hits++;char message[160];sprintf_s(message,"NPC hit accepted-shot=%u damage=%d health-before=%.1f",shot.sequence,damage,victim->m_fHealth);Log(message);
 }else if(entity->m_nType==ENTITY_TYPE_VEHICLE){
   hits++;
 }
 // Native impact effects preserve GTA's blood, material response and AI events.
 CWeapon impact(WEAPONTYPE_M4,1);
 // DoBulletImpact applies damage itself; scope the adapter damage instead of
 // generating a second damage event or permanently editing GTA weapon data.
 CWeaponInfo* info[3]{};unsigned short previous[3]{};
 for(unsigned char skill=0;skill<3;skill++){info[skill]=CWeaponInfo::GetWeaponInfo(WEAPONTYPE_M4,skill);previous[skill]=info[skill]->m_nDamage;info[skill]->m_nDamage=static_cast<unsigned short>(std::clamp(damage,1,65535));}
 impact.DoBulletImpact(player,entity,&origin,&end,&point,0);
 for(int skill=0;skill<3;skill++)info[skill]->m_nDamage=previous[skill];
}
void RunSelfTest(CPed* ped){
 if(!selfTest||testDone||!active)return;
 if(!testStarted){testStarted=GetTickCount();testStart=ped->GetPosition();Log("SELFTEST gameplay fixture started");}
 float seconds=(GetTickCount()-testStarted)/1000.0f;
 auto* pad=CPad::GetPad(0);pad->NewState.LeftStickX=0;pad->NewState.LeftStickY=seconds<2?-128:0;
 if(seconds>=2&&!testTarget){
  auto displacement=ped->GetPosition()-testStart;testDistance=std::hypot(displacement.x,displacement.y);
  float yr=yaw*Pi/180;CVector p=ped->GetPosition()+CVector(std::cos(yr),std::sin(yr),0)*3.0f;
  bool found=false;float ground=CWorld::FindGroundZFor3DCoord(p.x,p.y,p.z+3,&found,nullptr);if(found)p.z=ground+1.0f;
  CStreaming::RequestModel(7,2);CStreaming::LoadAllRequestedModels(false);
  if(auto* target=CPopulation::AddPed(PED_TYPE_CIVMALE,7,p,false)){testTarget=CPools::GetPedRef(target);target->m_fHealth=100;target->bStreamingDontDelete=true;Log("SELFTEST native NPC fixture spawned");}else{Log("SELFTEST FAILED NPC allocation");testDone=true;}
 }
 if(testTarget){
  if(auto* target=CPools::GetPed(testTarget)){
   auto delta=target->GetPosition()+CVector(0,0,.15f)-Eye(ped);float horizontal=std::sqrt(delta.x*delta.x+delta.y*delta.y);
   yaw=std::atan2(delta.y,delta.x)*180/Pi;pitch=-std::atan2(delta.z,horizontal)*180/Pi;
   if(seconds>=8){char result[200];sprintf_s(result,"SELFTEST %s movement=%.3f target-health=%.1f accepted=%u npc-hits=%u",target->m_fHealth<=0&&testDistance>.1f?"PASS":"FAILED",testDistance,target->m_fHealth,received,hits);Log(result);testDone=true;}
  }else if(seconds>=8){Log("SELFTEST FAILED target disappeared");testDone=true;}
 }
}
void Exchange(CPed* ped){
 if(!shared)return;
 if(requested&&ped)PublishCollision(ped);
 std::array<Shot,ShotCount> shots{};uint32_t count=0;
 std::array<Projectile,ProjectileCount> projectiles{};uint32_t projectileCount=0;
 std::array<Explosion,ExplosionCount> explosions{};uint32_t explosionCount=0;
 HostLock lockState=ComputeLock(ped);
 {
  Lock lock(controlMutex);if(!lock.acquired)return;
  auto* c=reinterpret_cast<Control*>(shared);guest=c->guest;guestMovement=c->guestMovement;
  guestCamera=*reinterpret_cast<GuestCamera*>(shared+CameraOffset);
  auto* feature=reinterpret_cast<Features*>(shared+FeaturesOffset);featureState=*feature;
  AutoResupply();
  uint32_t catalogSize=std::min(feature->catalogCount,CatalogCount);
  if(catalogSize&&weaponCatalog.size()!=catalogSize){auto* first=reinterpret_cast<CatalogEntry*>(shared+CatalogOffset);weaponCatalog.assign(first,first+catalogSize);for(auto& entry:weaponCatalog)entry.name[55]=0;}
  projectileCount=std::min(feature->projectileCount,ProjectileCount);
  memcpy(projectiles.data(),shared+ProjectilesOffset,projectileCount*sizeof(Projectile));
  uint32_t availableExplosions=feature->explosions-lastExplosion;
  if(availableExplosions>ExplosionCount){lastExplosion=feature->explosions-ExplosionCount;availableExplosions=ExplosionCount;Log("Explosion ring overflow");}
  while(availableExplosions--){++lastExplosion;auto& e=reinterpret_cast<Explosion*>(shared+ExplosionsOffset)[lastExplosion%ExplosionCount];if(e.sequence==lastExplosion)explosions[explosionCount++]=e;}
  feature->request=inventoryRequest;feature->kind=inventoryKind;feature->weapon=inventoryWeapon;
  auto* fixtureRequest=reinterpret_cast<uint32_t*>(shared+4544);
  if(active&&*fixtureRequest==0x54534554){*fixtureRequest=0;selfTest=true;testStarted=0;testDone=false;testTarget=0;testDistance=0;Log("Explicit NPC validation fixture requested");}
  auto* reactionRequest=reinterpret_cast<uint32_t*>(shared+4548);
  if(active&&*reactionRequest==0x41495453){*reactionRequest=0;reactionFixtureRequested=true;}
  auto* hudRequest=reinterpret_cast<uint32_t*>(shared+4556);
  if(active&&*hudRequest==0x48554454){*hudRequest=0;hudFixtureOriginalZone=CHud::m_pZoneName;hudFixtureUntil=GetTickCount()+5000;}
  uint32_t available=guest.shots-lastShot;
  if(available>ShotCount){Log("Shot ring overflow; discarding oldest expired records");lastShot=guest.shots-ShotCount;available=ShotCount;}
  while(available--){++lastShot;const auto& s=c->shots[lastShot%ShotCount];if(s.sequence==lastShot)shots[count++]=s;}
  host.tick=GetTickCount();host.enabled=active&&!settings&&!weaponMenu?1:0;
  host.yaw=yaw;host.pitch=pitch;host.fov=fov;host.health=ped?ped->m_fHealth:0;
  if(ped){auto p=Eye(ped);host.eye[0]=p.x;host.eye[1]=p.y;host.eye[2]=p.z;auto v=ped->m_vecMoveSpeed*50.0f;host.velocity[0]=v.x;host.velocity[1]=v.y;host.velocity[2]=v.z;}
  host.buttons=0;
  if(active&&!settings&&!weaponMenu&&Focused()){
   if(Down(VK_LBUTTON))host.buttons|=1;if(Down(VK_RBUTTON))host.buttons|=2;
   // Weapon switching: 2 or wheel down for next, 1 or wheel up for previous.
   // A wheel notch lasts one frame; hold it briefly so IW4 sees the press.
   static DWORD wheelNextUntil=0,wheelPrevUntil=0;
   if(CPad::NewMouseControllerState.wheelDown)wheelNextUntil=GetTickCount()+120;
   if(CPad::NewMouseControllerState.wheelUp)wheelPrevUntil=GetTickCount()+120;
   if(Down('R'))host.buttons|=4;
   if(Down('2')||static_cast<int>(wheelNextUntil-GetTickCount())>0)host.buttons|=8;
   if(Down('1')||static_cast<int>(wheelPrevUntil-GetTickCount())>0)host.buttons|=65536;
   if(Down('W'))host.buttons|=16;if(Down('S'))host.buttons|=32;if(Down('A'))host.buttons|=64;if(Down('D'))host.buttons|=128;
   if(Down(VK_SHIFT))host.buttons|=256;if(Down(VK_SPACE))host.buttons|=512;if(Down('C'))host.buttons|=1024;if(Down(VK_CONTROL))host.buttons|=2048;
   if(Down('G'))host.buttons|=4096;if(Down('H'))host.buttons|=8192;
   if(Down('V'))host.buttons|=16384;if(Down('E'))host.buttons|=32768;
  }
  if(selfTest && testStarted && !testDone){float seconds=(GetTickCount()-testStarted)/1000.0f;if(seconds>=4&&seconds<7)host.buttons|=3;}
  if(selfTest&&testStarted&&!testDone&&(GetTickCount()-testStarted)<2000)host.buttons|=16;
  hostMovement.tick=host.tick;hostMovement.flags=active?1:0;
  memcpy(&hostMovement.reserved[0],&mouseTotalX,sizeof(float));
  memcpy(&hostMovement.reserved[1],&mouseTotalY,sizeof(float));
  hostMovement.reserved[2]=selfTest&&!testDone?1:0;
  ApplyReactionFixtureInput(ped);
  c->host=host;c->hostMovement=hostMovement;c->lock=lockState;
 }
 for(uint32_t i=0;i<count;i++)ConsumeShot(shots[i],ped);
 UpdateGrenades(projectiles.data(),projectileCount);
 TriggerClaymores(projectiles.data(),projectileCount,ped);
 ThrowingKnifeHits(projectiles.data(),projectileCount,ped);
 TriggerSmoke(projectiles.data(),projectileCount);
 for(uint32_t i=0;i<explosionCount;i++)ConsumeExplosion(explosions[i],ped);
}
void AfterInput(){
 InputTimer timer;
 CPed* ped=FindPlayerPed();bool focused=Focused();
 MonitorHealth(ped);
 ProbeReactionFixture(ped);
 if(selfTest&&!requested&&!testStarted&&ped&&!CCutsceneMgr::ms_running){requested=StartRuntime();}
 if(focused&&Edge(VK_F6)){requested=!requested;if(requested&&!StartRuntime())requested=false;Log(requested?"MW2 requested":"MW2 disabled");}
 if(focused&&active&&Edge(VK_F7)){weaponMenu=false;settings=!settings;if(settings){uiX=RsGlobal.maximumWidth*(.25f+.5f*(fov-50)/60);uiY=RsGlobal.maximumHeight*.45f;}else SaveFov();}
 if(focused&&active)InventoryInput();
 UpdateInventorySafety(ped);
 bool ready=guest.ready&&GetTickCount()-guest.tick<1500;
 if(selfTest && GetFileAttributesW(L"MW2GTA.selftest")!=INVALID_FILE_ATTRIBUTES && ped && CPad::GetPad(0)->DisablePlayerControls==0){DeleteFileW(L"MW2GTA.selftest");testStarted=0;testDone=false;testTarget=0;testDistance=0;Log("SELFTEST fixture reset requested");}
 // F near a vehicle hands the player to GTA to board it, as missions require.
 // GTA sees F as freshly pressed because the active mod cleared OldState; the
 // mod resumes on foot after the player leaves the vehicle.
 static DWORD vehicleHandoffUntil=0;
 if(active&&focused&&!weaponMenu&&!settings&&Edge('F')&&ped){
  std::array<CEntity*,8> vehiclesNear{};short n=0;
  CWorld::FindObjectsInRange(ped->GetPosition(),5.0f,false,&n,static_cast<short>(vehiclesNear.size()),vehiclesNear.data(),false,true,false,false,false);
  if(n>0){vehicleHandoffUntil=GetTickCount()+5000;Log("Vehicle handoff to GTA");}
 }
 if(ped&&ped->bInVehicle)vehicleHandoffUntil=0;
 bool handoff=GetTickCount()<vehicleHandoffUntil;
 bool scripted=CCutsceneMgr::ms_running||CPad::GetPad(0)->DisablePlayerControls!=0||TheCamera.m_bWideScreenOn;
 bool allowed=requested&&ready&&ped&&ped->m_fHealth>0&&!ped->bInVehicle&&!handoff&&!FrontEndMenuManager.m_bMenuActive&&!CTimer::m_UserPause&&!CTimer::m_CodePause&&!scripted;
 if(!allowed){Restore(!scripted);Exchange(ped);UpdateRuntimeRestart();return;}
 if(!active){
  controlled=ped;previousVisible=ped->bIsVisible;previousWeapon=ped->m_nSelectedWepSlot;
  SaveControlBlocks(CPad::GetPad(0));
  CVector front=TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecFront;
  yaw=std::atan2(front.y,front.x)*180/Pi;pitch=std::asin(std::clamp(-front.z,-1.0f,1.0f))*180/Pi;
  active=true;BeginMovement(ped);BeginWorldReaction();Log("IW4 pmove authority active; GTA locomotion disabled");
 }
 DWORD now=GetTickCount();host.dt=std::clamp((now-lastTime)/1000.0f,0.001f,0.1f);lastTime=now;
 // GTA flips its mouse movement by its own "invert mouse" options (except
 // while its menu is up). MW2 takes the real movement, plus its own option.
 CVector2D mouse(CPad::NewMouseControllerState.x,CPad::NewMouseControllerState.y);
 if(!*reinterpret_cast<uint8_t*>(0xBA67A4)){if(CMenuManager::bInvertMouseX)mouse.x=-mouse.x;if(CMenuManager::bInvertMouseY)mouse.y=-mouse.y;}
 if(focused&&settings){uiX=std::clamp(uiX+mouse.x*2.0f,0.0f,static_cast<float>(RsGlobal.maximumWidth));uiY=std::clamp(uiY+mouse.y*2.0f,0.0f,static_cast<float>(RsGlobal.maximumHeight));}
 if(focused&&!settings&&!weaponMenu){mouseTotalX+=mouse.x;mouseTotalY+=invertMouse?-mouse.y:mouse.y;}
 auto* pad=CPad::GetPad(0);
 MaintainWorldReaction();
 pad->bDisablePlayerEnterCar=pad->bDisablePlayerDuck=pad->bDisablePlayerFireWeapon=pad->bDisablePlayerFireWeaponWithL1=pad->bDisablePlayerCycleWeapon=pad->bDisablePlayerJump=pad->bDisablePlayerDisplayVitalStats=1;
 ped->m_nSelectedWepSlot=0;
 // IW4 reads Win32 input separately. Native on-foot actions must never run
 // alongside it, including F/Enter boarding vehicles and wheel cycling.
 pad->NewState={};pad->OldState={};
 ped->bApplyGravity=false;ped->bDontApplySpeed=true;ped->m_vecMoveSpeed=CVector(0,0,0);
 ped->bDisableCollisionForce=true;ped->bDisableMoveForce=true;ped->bDisableTurnForce=true;
 CPad::NewMouseControllerState.lmb=0;CPad::NewMouseControllerState.rmb=0;
 if(settings){pad->NewState.LeftStickX=0;pad->NewState.LeftStickY=0;}
 RunSelfTest(ped);
 if(testDone)selfTest=false;
 Exchange(ped);
}
void UpdateCamera(CCamera* camera);
void DistrictPrint(float x,float y,char* text){
 if(active){y=std::min(y,static_cast<float>(RsGlobal.maximumHeight)*.30f);static char* previous=nullptr;if(previous!=text){char line[160];sprintf_s(line,"GTA district label moved above weapon HUD: y=%.1f text=%.80s",y,text?text:"");Log(line);previous=text;}}
 reinterpret_cast<void(__cdecl*)(float,float,char*)>(districtPrintOriginal)(x,y,text);
}
void PrepareHostRender(){if(active&&controlled)ApplyMovement(controlled);UpdateCamera(&TheCamera);}
void AfterScripts(){if(active&&!CCutsceneMgr::ms_running)MaintainWorldReaction();}
void HidePlayerForScene(){if(active&&controlled&&controlled==FindPlayerPed())controlled->bIsVisible=false;}
void RestorePlayerAfterScene(){if(active&&controlled&&controlled==FindPlayerPed())controlled->bIsVisible=previousVisible;}
void UpdateCamera(CCamera* camera){
 if(!active||!controlled||!camera->m_pRwCamera)return;
 float yr=yaw*Pi/180,pr=pitch*Pi/180;float sy=std::sin(yr),cy=std::cos(yr),sp=std::sin(pr),cp=std::cos(pr);
 CVector forward(cp*cy,cp*sy,-sp),right(-sy,cy,0),up(sp*cy,sp*sy,cp),position=Eye(controlled);
 float effective=fov;
 // A delayed guest frame must hold its last valid camera, rather than snap
 // back to the activation yaw/height and then jump forward on recovery.
 if(guestCamera.epoch==hostMovement.epoch&&guestCamera.sequence){
  position=CVector(guestCamera.eye[0],guestCamera.eye[1],guestCamera.eye[2]);
  forward=CVector(guestCamera.forward[0],guestCamera.forward[1],guestCamera.forward[2]);
  right=CVector(guestCamera.right[0],guestCamera.right[1],guestCamera.right[2]);
  up=CVector(guestCamera.up[0],guestCamera.up[1],guestCamera.up[2]);
  effective=guestCamera.fov;
 }
 auto& cam=camera->m_aCams[camera->m_nActiveCam];
 cam.m_vecSource=position;cam.m_vecFront=forward;cam.m_vecUp=up;
 cam.m_fFOV=effective;
 auto& matrix=camera->m_mCameraMatrix;
 matrix.right=right;matrix.up=forward;matrix.at=up;matrix.pos=position;
 camera->m_vecGameCamPos=position;
 CDraw::ms_fFOV=effective; // Keep native CameraSize and culling at the host FOV.
 camera->CopyCameraMatrixToRWCam(true);
 RwV2d viewWindow{std::tan(effective*Pi/360),std::tan(effective*Pi/360)};
 if(RsGlobal.maximumHeight>0)viewWindow.y/=static_cast<float>(RsGlobal.maximumWidth)/RsGlobal.maximumHeight;
 RwCameraSetViewWindow(camera->m_pRwCamera,&viewWindow);
 RwCameraSetNearClipPlane(camera->m_pRwCamera,0.5f);
 camera->CalculateDerivedValues(false,true);
}
void __fastcall CameraHook(CCamera* camera,void*){
 if(active&&controlled)ApplyMovement(controlled);
 reinterpret_cast<void(__thiscall*)(CCamera*)>(cameraOriginal)(camera);
 UpdateCamera(camera);
}
struct Vertex {float x,y,z,rhw;DWORD color;float u,v;};
void Quad(IDirect3DDevice9* device,float x,float y,float w,float h,DWORD color,IDirect3DTexture9* tex=nullptr){
 Vertex vertices[4]={{x-.5f,y-.5f,0,1,color,0,0},{x+w-.5f,y-.5f,0,1,color,1,0},{x-.5f,y+h-.5f,0,1,color,0,1},{x+w-.5f,y+h-.5f,0,1,color,1,1}};
 device->SetTexture(0,tex);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
}
void DrawHostVitals(IDirect3DDevice9* device,float w,float h){
 auto* ped=FindPlayerPed();if(!ped)return;
 float maximum=std::max(1.0f,static_cast<float>(CWorld::Players[0].m_nMaxHealth));
 float health=std::clamp(ped->m_fHealth/maximum,0.0f,1.0f);
 float x=w*.025f,y=h*.83f,width=w*.15f,height=std::max(8.0f,h*.012f);
 Quad(device,x-4,y-4,width+8,height+8,0xd0181818);Quad(device,x,y,width,height,0xff505050);
 if(health>0)Quad(device,x,y,width*health,height,health<.3f?0xffdf4545:0xff7ed091);
 auto* wanted=FindPlayerWanted();unsigned stars=wanted?std::min(wanted->m_nWantedLevel,6u):0;
 float radius=h*.012f;
 for(unsigned s=0;s<6;s++){
  float cx=w*.97f-(5-s)*radius*2.6f,cy=h*.075f;DWORD color=s<stars?0xffffd550:0xb0707070;
  Vertex vertices[12]{};vertices[0]={cx,cy,0,1,color,0,0};
  for(int i=0;i<=10;i++){float angle=-Pi*.5f+(i%10)*Pi*.2f,r=(i%2?radius*.43f:radius);vertices[i+1]={cx+std::cos(angle)*r,cy+std::sin(angle)*r,0,1,color,0,0};}
  device->SetTexture(0,nullptr);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,10,vertices,sizeof(Vertex));
 }
 DWORD now=GetTickCount();
 if(now<damageFeedbackUntil){unsigned alpha=static_cast<unsigned>(150*damageFeedbackStrength*(damageFeedbackUntil-now)/850.0f);DWORD color=(alpha<<24)|0x00b00000;
  Quad(device,0,0,w,h*.045f,color);Quad(device,0,h*.955f,w,h*.045f,color);Quad(device,0,h*.045f,w*.025f,h*.91f,color);Quad(device,w*.975f,h*.045f,w*.025f,h*.91f,color);
 }
}
void CaptureGame(IDirect3DDevice9* device){
 if(GetFileAttributesW(L"MW2GTA.capture")==INVALID_FILE_ATTRIBUTES)return;
 IDirect3DSurface9* target=nullptr;IDirect3DSurface9* resolved=nullptr;IDirect3DSurface9* cpu=nullptr;
 if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&target)))return;
 D3DSURFACE_DESC desc{};target->GetDesc(&desc);
 HRESULT status=S_OK;
 if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){status=device->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr);if(SUCCEEDED(status))status=device->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE);}
 if(SUCCEEDED(status))status=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&cpu,nullptr);
 if(SUCCEEDED(status))status=device->GetRenderTargetData(resolved?resolved:target,cpu);
 D3DLOCKED_RECT data{};
 if(SUCCEEDED(status)&&SUCCEEDED(cpu->LockRect(&data,nullptr,D3DLOCK_READONLY))){
  if(desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8){
   FILE* file=nullptr;_wfopen_s(&file,L"MW2GTA.capture.bmp",L"wb");
   if(file){BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+desc.Width*desc.Height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=desc.Width;info.biHeight=-static_cast<LONG>(desc.Height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    fwrite(&header,sizeof(header),1,file);fwrite(&info,sizeof(info),1,file);for(UINT y=0;y<desc.Height;y++)fwrite(static_cast<const uint8_t*>(data.pBits)+y*data.Pitch,desc.Width*4,1,file);fclose(file);DeleteFileW(L"MW2GTA.capture");Log("Native D3D9 screenshot written");
   }
  }
  cpu->UnlockRect();
 }
 if(cpu)cpu->Release();if(resolved)resolved->Release();target->Release();
}
using PresentFunction=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
PresentFunction originalPresent=nullptr;
HRESULT WINAPI PresentHook(IDirect3DDevice9* device,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty){CaptureGame(device);auto begin=Counter();auto result=originalPresent(device,source,destination,window,dirty);RecordFrame(begin,Counter());return result;}
void InstallCaptureHook(){
 auto* device=static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());if(!device||originalPresent)return;
 CreateChromaShader(device);
 auto** table=*reinterpret_cast<void***>(device);originalPresent=reinterpret_cast<PresentFunction>(table[17]);
 if(!originalPresent)return;DWORD old=0;if(VirtualProtect(table+17,sizeof(void*),PAGE_EXECUTE_READWRITE,&old)){table[17]=reinterpret_cast<void*>(PresentHook);DWORD ignored;VirtualProtect(table+17,sizeof(void*),old,&ignored);Log("Installed private framebuffer capture hook");}
}
// F7 checkboxes, saved to MW2GTA.ini. Help text is not saved: it starts
// shown every session.
struct SettingOption {bool* value;const char* label;const wchar_t* key;};
const SettingOption Settings[]={
 {&showHelpText,"Show top help text",L"ShowHelpText"},
 {&infiniteAmmo,"Infinite ammo (auto refill)",L"InfiniteAmmo"},
 {&healthRegen,"MW2 health regeneration",L"HealthRegen"},
 {&peacefulWorld,"Peaceful world (police and NPCs leave you alone)",L"PeacefulWorld"},
 {&invertMouse,"Invert MW2 mouse (up/down)",L"InvertMouse"},
};
constexpr int SettingCount=static_cast<int>(std::size(Settings));
void Draw(){
 if(!shared)return;
 if(hudFixtureUntil){static char label[]="MW2 HUD TEST";if(GetTickCount()<hudFixtureUntil){CHud::m_pZoneName=label;CHud::m_pLastZoneName=label;CHud::m_ZoneToPrint=label;CHud::m_ZoneState=1;CHud::m_ZoneFadeTimer=1000;CHud::m_ZoneNameTimer=0;}else{CHud::m_pZoneName=hudFixtureOriginalZone;CHud::m_pLastZoneName=nullptr;CHud::m_ZoneState=0;hudFixtureUntil=0;}}
 {Lock lock(controlMutex);if(lock.acquired)reinterpret_cast<Control*>(shared)->host.tick=GetTickCount();}
 ++renderedFrames;
 if(child.hProcess&&GetTickCount()-lastReport>1000){WatchRuntime();EnumWindows(MoveGuestWindow,child.dwProcessId);lastReport=GetTickCount();char msg[320];sprintf_s(msg,"status requested=%d active=%d guest-ready=%u frame=%u accepted=%u hits=%u ammo=%d vehicle=%d controls=%u cutscene=%d",requested,active,guest.ready,lastFrame,received,hits,guest.clip,FindPlayerPed()?FindPlayerPed()->bInVehicle:0,CPad::GetPad(0)->DisablePlayerControls,CCutsceneMgr::ms_running);Log(msg);
  float seconds=std::max(.001f,(GetTickCount()-lastFpsTime)/1000.0f);char perf[200];sprintf_s(perf,"performance host-fps=%.1f texture-fps=%.1f movement=%u movement-age=%lu triangles=%zu sprint=%u grounded=%u",renderedFrames/seconds,uploadedFrames/seconds,guestMovement.sequence,GetTickCount()-guestMovement.tick,sceneTriangles.size(),(guestMovement.pmFlags&0x4000)!=0,guestMovement.grounded);Log(perf);renderedFrames=0;uploadedFrames=0;lastFpsTime=GetTickCount();
  // Whether MW2 drew its gun (sampled non-key pixels) and whether GTA has the
  // player near or in fire: separates a missing guest image from a host one.
  if(active){size_t drawn=0,sampled=0;for(size_t i=0;i+3<pixels.size();i+=4*97){const uint8_t* q=pixels.data()+i;sampled++;if(!(q[0]>200&&q[2]>200&&q[1]<60))drawn++;}
   auto* ped=FindPlayerPed();char view[200];sprintf_s(view,"overlay drawn=%zu/%zu texture=%d on-fire=%d health=%.0f",drawn,sampled,texture!=nullptr,ped&&ped->m_pFire!=nullptr,ped?ped->m_fHealth:0.0f);Log(view);}
  if(active)if(auto* wanted=FindPlayerWanted()){char line[200];sprintf_s(line,"GTA pursuit wanted=%u chaos=%u cops=%u/%u cap=%u backoff=%d/%d/%d/%d health=%.1f",wanted->m_nWantedLevel,wanted->m_nChaosLevel,wanted->m_nCopsInPursuit,wanted->m_nMaxCopsInPursuit,CWanted::MaximumWantedLevel,wanted->m_bPoliceBackOff,wanted->m_bEverybodyBackOff,wanted->m_bPoliceBackOffGarage,wanted->m_bLeavePlayerAlone,FindPlayerPed()?FindPlayerPed()->m_fHealth:0);Log(line);}
 }
 if(!FrontEndMenuManager.m_bMenuActive && (requested||active||!runtimeError.empty())){
  CFont::SetScale(.6f,1.0f);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetColor(CRGBA(255,255,255,255));CFont::SetDropColor(CRGBA(0,0,0,255));CFont::SetEdge(1);CFont::SetProportional(true);CFont::SetOrientation(ALIGN_LEFT);CFont::SetBackground(false,false);
  char status[260];
  if(active && settings)sprintf_s(status,"MW2 FOV %.0f - drag slider, click options - F7 close",fov);
  else if(active&&!showHelpText)status[0]=0;
  else if(active)sprintf_s(status,"MW2 F8 weapons / 1-2 or wheel switch / F9 ammo / G grenade/explosives (%d) / H tactical (%d) / V melee / E use / F7 more options - ammo %d / %d - HP %.0f / wanted %u",featureState.lethalAmmo,featureState.tacticalAmmo,guest.clip,guest.stock,FindPlayerPed()?std::max(0.0f,FindPlayerPed()->m_fHealth):0.0f,FindPlayerWanted()?FindPlayerWanted()->m_nWantedLevel:0);
  else if(!runtimeError.empty())sprintf_s(status,"%s",runtimeError.c_str());
  else if(guest.ready&&GetTickCount()-guest.tick<1500){auto* ped=FindPlayerPed();sprintf_s(status,ped&&ped->bInVehicle?"MW2 ready - leave vehicle to use - F6 exit":"MW2 ready - waiting for GTA controls - F6 exit");}
  else sprintf_s(status,"MW2 loading weapons... %lus - F6 cancels",(GetTickCount()-runtimeStarted)/1000);
  if(active&&!settings)CFont::SetScale(.42f,.8f);
  // Top left, clear of the MW2 gun and hands; wrap before GTA's money,
  // weapon and clock in the top right.
  CFont::SetWrapx(static_cast<float>(RsGlobal.maximumWidth)*.68f);
  if(status[0])CFont::PrintString(15.0f,12.0f,status);
  CFont::SetWrapx(static_cast<float>(RsGlobal.maximumWidth));
 }
 if(!active||FrontEndMenuManager.m_bMenuActive)return;
 auto* device=static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());if(!device)return;
 uint32_t sequence=0;
 {Lock lock(frameMutex);if(lock.acquired){auto* frame=reinterpret_cast<Frame*>(shared+FrameOffset);if(frame->sequence!=lastFrame&&frame->width&&frame->height&&frame->width<=MaxWidth&&frame->height<=MaxHeight){imageWidth=frame->width;imageHeight=frame->height;sequence=frame->sequence;pixels.assign(shared+PixelOffset,shared+PixelOffset+imageWidth*imageHeight*4);}}}
 if(sequence){
  D3DSURFACE_DESC desc{};if(texture)texture->GetLevelDesc(0,&desc);
  if(texture&&(desc.Width!=imageWidth||desc.Height!=imageHeight))ReleaseTexture();
  if(!texture)device->CreateTexture(imageWidth,imageHeight,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr);
  D3DLOCKED_RECT rectangle{};if(texture&&SUCCEEDED(texture->LockRect(0,&rectangle,nullptr,D3DLOCK_DISCARD))){for(uint32_t y=0;y<imageHeight;y++){auto* row=static_cast<uint8_t*>(rectangle.pBits)+y*rectangle.Pitch;auto* source=pixels.data()+y*imageWidth*4;if(chromaShader)memcpy(row,source,imageWidth*4);else CpuChroma(row,source,imageWidth);}texture->UnlockRect(0);lastFrame=sequence;++uploadedFrames;}
 }
 IDirect3DStateBlock9* saved=nullptr;if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&saved)))return;saved->Capture();
 device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
 device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
 device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
 // GTA's fire and heat-haze passes leave stencil testing, fog, clip planes
 // and sometimes a smaller offscreen target behind. Inherited, they cut the
 // overlay down to the fire's sprites or hide it. The state block restores
 // all of these; the render target is restored below.
 device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
 device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
 device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
 IDirect3DSurface9* previousTarget=nullptr;IDirect3DSurface9* backBuffer=nullptr;bool retarget=false;
 if(SUCCEEDED(device->GetRenderTarget(0,&previousTarget))&&SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer))&&previousTarget!=backBuffer){
  D3DSURFACE_DESC current{},screen{};previousTarget->GetDesc(&current);backBuffer->GetDesc(&screen);
  // A same-sized target can be a graphics mod's own screen buffer; keep it.
  retarget=current.Width!=screen.Width||current.Height!=screen.Height;
  if(retarget)device->SetRenderTarget(0,backBuffer);
 }
 {D3DVIEWPORT9 full{0,0,static_cast<DWORD>(RsGlobal.maximumWidth),static_cast<DWORD>(RsGlobal.maximumHeight),0.0f,1.0f};device->SetViewport(&full);}
 float w=static_cast<float>(RsGlobal.maximumWidth),h=static_cast<float>(RsGlobal.maximumHeight);
 if(texture){device->SetPixelShader(chromaShader);Quad(device,0,0,w,h,0xffffffff,texture);device->SetPixelShader(nullptr);}
 device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
 DrawHostVitals(device,w,h);
 if(weaponMenu)Quad(device,w*.18f,h*.15f,w*.64f,h*.65f,0xf0181818);
 if(settings){
  float x=w*.25f,y=h*.3f,width=w*.5f;
  Quad(device,x-20,y-30,width+40,95+SettingCount*40.0f,0xe0202020);Quad(device,x,y,width,10,0xff707070);
  float position=(fov-50)/60;Quad(device,x+position*width-5,y-6,10,22,0xffffffff);
  POINT cursor{static_cast<LONG>(uiX),static_cast<LONG>(uiY)};
  Quad(device,uiX-5,uiY-1,10,2,0xff00ff00);Quad(device,uiX-1,uiY-5,2,10,0xff00ff00);
  if(Down(VK_LBUTTON)&&cursor.x>=x-10&&cursor.x<=x+width+10&&cursor.y>=y-20&&cursor.y<=y+30){fov=std::clamp(50+(cursor.x-x)/width*60,50.0f,110.0f);SaveFov();}
  // Checkbox rows: click the box or its label.
  static bool clickHeld=false;bool click=Down(VK_LBUTTON)&&!clickHeld;clickHeld=Down(VK_LBUTTON);
  {
   float rowY=y+45+SettingCount*40.0f;
   Quad(device,x,rowY,22,22,0xffd0d0d0);Quad(device,x+3,rowY+3,16,16,0xff3070d0);
   if(click&&cursor.x>=x-5&&cursor.x<=x+width&&cursor.y>=rowY-6&&cursor.y<=rowY+28){
    resolutionChoice=(resolutionChoice+1)%ResolutionChoices;
    wchar_t value[8];swprintf_s(value,L"%d",resolutionChoice);WritePrivateProfileStringW(L"Bridge",L"Resolution",value,iniPath.c_str());
    RequestRuntimeRestart();
   }
  }
  for(int i=0;i<SettingCount;i++){
   float rowY=y+45+i*40.0f;
   Quad(device,x,rowY,22,22,0xffd0d0d0);Quad(device,x+3,rowY+3,16,16,*Settings[i].value?0xff30c030:0xff303030);
   if(click&&cursor.x>=x-5&&cursor.x<=x+width&&cursor.y>=rowY-6&&cursor.y<=rowY+28){*Settings[i].value=!*Settings[i].value;WritePrivateProfileStringW(L"Bridge",Settings[i].key,*Settings[i].value?L"1":L"0",iniPath.c_str());}
  }
 }
 if(retarget)device->SetRenderTarget(0,previousTarget);
 if(previousTarget)previousTarget->Release();if(backBuffer)backBuffer->Release();
 saved->Apply();saved->Release();
 // GTA text only after the device state is restored; inside the raw D3D
 // section it renders with the overlay's state and corrupts the frame.
 if(settings){
  float x=w*.25f,y=h*.3f;
  CFont::SetScale(.5f,.85f);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetColor(CRGBA(255,255,255,255));CFont::SetDropColor(CRGBA(0,0,0,255));CFont::SetEdge(1);CFont::SetProportional(true);CFont::SetOrientation(ALIGN_LEFT);CFont::SetBackground(false,false);
  for(int i=0;i<SettingCount;i++)CFont::PrintString(x+34,y+43+i*40.0f,Settings[i].label);
  uint32_t rw=0,rh=0;ResolutionFor(resolutionChoice,rw,rh);
  char resolution[120];sprintf_s(resolution,"MW2 resolution: %s (%ux%u) - click to change, restarts MW2",ResolutionLabels[resolutionChoice],rw,rh);
  CFont::PrintString(x+34,y+43+SettingCount*40.0f,resolution);
 }
 DrawInventory();
}
void Shutdown(){
 Restore();ReleaseTexture();if(chromaShader){chromaShader->Release();chromaShader=nullptr;}
 if(shared){Lock lock(controlMutex);if(lock.acquired)reinterpret_cast<Control*>(shared)->host.shutdown=1;}
 if(child.hThread)CloseHandle(child.hThread);if(child.hProcess)CloseHandle(child.hProcess);
 if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);if(controlMutex)CloseHandle(controlMutex);if(frameMutex)CloseHandle(frameMutex);if(sceneMutex)CloseHandle(sceneMutex);
 if(logFile){fclose(logFile);logFile=nullptr;}
}
struct Plugin {
 Plugin(){
  wchar_t directory[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,directory);iniPath=std::wstring(directory)+L"\\MW2GTA.ini";
  logFile=_wfsopen((std::wstring(directory)+L"\\MW2GTA.log").c_str(),L"w",_SH_DENYNO);
  if(!plugin::IsGameVersion10us()){Log("Unsupported GTA executable; plugin left inactive");return;}
  wchar_t runtime[MAX_PATH];GetPrivateProfileStringW(L"Bridge",L"Runtime",L"",runtime,MAX_PATH,iniPath.c_str());runtimeRoot=runtime;
  wchar_t identity[128],executable[128];GetPrivateProfileStringW(L"Bridge",L"MappingName",L"MW2GTA_v1",identity,128,iniPath.c_str());bridgeName=identity;GetPrivateProfileStringW(L"Bridge",L"RuntimeExecutable",L"iw4l-gta.exe",executable,128,iniPath.c_str());runtimeExecutable=executable;
  fov=std::clamp(ReadFloat(L"FOV",80),50.0f,110.0f);eyeHeight=std::clamp(ReadFloat(L"EyeOffset",.72f),.3f,1.1f);sensitivity=std::clamp(ReadFloat(L"Sensitivity",.13f),.01f,1.0f);
  selfTest=GetPrivateProfileIntW(L"Bridge",L"SelfTest",0,iniPath.c_str())!=0;
  resolutionChoice=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Bridge",L"Resolution",2,iniPath.c_str())),0,ResolutionChoices-1);
  infiniteAmmo=GetPrivateProfileIntW(L"Bridge",L"InfiniteAmmo",0,iniPath.c_str())!=0;
  healthRegen=GetPrivateProfileIntW(L"Bridge",L"HealthRegen",1,iniPath.c_str())!=0;
  peacefulWorld=GetPrivateProfileIntW(L"Bridge",L"PeacefulWorld",0,iniPath.c_str())!=0;
  invertMouse=GetPrivateProfileIntW(L"Bridge",L"InvertMouse",0,iniPath.c_str())!=0;
  if(!CreateBridge()){Log("Failed to initialize shared memory");return;}
  constexpr uintptr_t call=0x53C104;
  if(*reinterpret_cast<uint8_t*>(call)!=0xe8){Log("Camera call site changed; plugin left inactive");return;}
  cameraOriginal=call+5+*reinterpret_cast<int32_t*>(call+1);
  MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(cameraOriginal),&info,sizeof(info));
  if(!cameraOriginal||!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))){Log("Invalid camera callback; plugin left inactive");return;}
  injector::MakeCALL(call,CameraHook,true);
  constexpr uintptr_t districtCall=0x58AE5D;
  if(*reinterpret_cast<uint8_t*>(districtCall)==0xE8){districtPrintOriginal=districtCall+5+*reinterpret_cast<int32_t*>(districtCall+1);injector::MakeCALL(districtCall,DistrictPrint,true);}
  static plugin::CdeclEvent<plugin::AddressList<0x53BEE6,plugin::H_CALL>,plugin::PRIORITY_AFTER,plugin::ArgPickNone,void()> afterInput;
  afterInput+=AfterInput;
  static plugin::CdeclEvent<plugin::AddressList<0x53BEE6,plugin::H_CALL>,plugin::PRIORITY_BEFORE,plugin::ArgPickNone,void()> beforeInput;
  beforeInput+=BeforeInput;
  static plugin::CdeclEvent<plugin::AddressList<0x53BFC7,plugin::H_CALL>,plugin::PRIORITY_AFTER,plugin::ArgPickNone,void()> afterScripts;
  afterScripts+=AfterScripts;
  static plugin::CdeclEvent<plugin::AddressList<0x53E9F9,plugin::H_CALL>,plugin::PRIORITY_BEFORE,plugin::ArgPickNone,void()> beforeRenderList;
  beforeRenderList+=PrepareHostRender;
  static plugin::CdeclEvent<plugin::AddressList<0x53EABF,plugin::H_CALL>,plugin::PRIORITY_BEFORE,plugin::ArgPickNone,void()> beforeScene;
  beforeScene+=HidePlayerForScene;
  plugin::Events::renderSceneEvent+=RestorePlayerAfterScene;
  plugin::Events::initRwEvent+=InstallCaptureHook;
  plugin::Events::drawingEvent+=Draw;
  plugin::Events::d3dLostEvent+=ReleaseTexture;
  plugin::Events::shutdownRwEvent+=Shutdown;
  QueryPerformanceFrequency(&perfFrequency);lastTime=lastFpsTime=GetTickCount();Log("Installed bridge; F6 enables MW2, F7 opens FOV slider");
 }
} instance;
}
