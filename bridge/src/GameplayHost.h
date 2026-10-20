#pragma once
// GTA 1.0 US adapter. IW4 owns accepted fire/equipment; GTA owns its NPC AI,
// crimes, pursuit, native effects and damage reactions.
CWanted* reactionWanted=nullptr;
unsigned savedMaximumWanted=6;
bool changedWantedCap=false,savedPoliceBackOff=false,savedEverybodyBackOff=false,savedLeaveAlone=false;
bool savedGarageBackOff=false;float savedCrimeMultiplier=1.0f;
DWORD damageFeedbackUntil=0;float damageFeedbackStrength=0;
// F7 options, saved in MW2GTA.ini.
bool showHelpText=true,infiniteAmmo=false,healthRegen=true,peacefulWorld=false,invertMouse=false,announcerOn=true;
float announcerVolume=2.0f;
// Size of the mod's own HUD text (killstreaks, pickups, notices), F7.
float hudTextScale=0.7f;
// GTA has no C4: its timed dynamite charge is the nearest explosive block.
constexpr int C4Model=1654;
struct GrenadeProxy {uint32_t id=0;int reference=0;DWORD seen=0;CVector rest{};bool triggered=false;CVector last{};bool hasLast=false;uint32_t weapon=0;CVector position{};};
std::array<GrenadeProxy,ProjectileCount> grenadeProxies{};
CPed* protectedMenuPlayer=nullptr;
std::array<bool,6> previousMenuProof{};
void RestoreInventorySafety(){
 if(protectedMenuPlayer&&protectedMenuPlayer==FindPlayerPed()){
  protectedMenuPlayer->bInvulnerable=previousMenuProof[0];protectedMenuPlayer->bBulletProof=previousMenuProof[1];protectedMenuPlayer->bExplosionProof=previousMenuProof[2];protectedMenuPlayer->bCollisionProof=previousMenuProof[3];protectedMenuPlayer->bFireProof=previousMenuProof[4];protectedMenuPlayer->bMeleeProof=previousMenuProof[5];
 }
 protectedMenuPlayer=nullptr;
}
void UpdateInventorySafety(CPed* ped){
 if(!active||(!weaponMenu&&!menuAcceptHeld)||!ped){RestoreInventorySafety();return;}
 if(!protectedMenuPlayer){protectedMenuPlayer=ped;previousMenuProof={ped->bInvulnerable!=0,ped->bBulletProof!=0,ped->bExplosionProof!=0,ped->bCollisionProof!=0,ped->bFireProof!=0,ped->bMeleeProof!=0};}
 ped->bInvulnerable=ped->bBulletProof=ped->bExplosionProof=ped->bCollisionProof=ped->bFireProof=ped->bMeleeProof=true;
}
void MonitorHealth(CPed* ped){
 static CPed* previous=nullptr;static float health=0;static DWORD lastDamage=0,lastTick=0;
 DWORD now=GetTickCount();float seconds=lastTick?std::min((now-lastTick)/1000.0f,.1f):0;lastTick=now;
 if(ped&&ped==previous&&ped->m_fHealth<health-.01f)lastDamage=now;
 // MW2 regeneration: after five seconds without damage, recover to full in
 // about three seconds. Dead players and GTA's own death handling are left alone.
 if(healthRegen&&active&&ped&&ped==previous&&ped->m_fHealth>0&&now-lastDamage>5000&&ped->m_fHealth<ped->m_fMaxHealth){
  ped->m_fHealth=std::min(ped->m_fMaxHealth,ped->m_fHealth+ped->m_fMaxHealth*seconds/3.0f);
  health=ped->m_fHealth;
 }
 if(ped&&ped==previous&&ped->m_fHealth<health-.01f){if(active){damageFeedbackUntil=GetTickCount()+850;damageFeedbackStrength=std::clamp((health-ped->m_fHealth)/35.0f,.35f,1.0f);}char line[200];sprintf_s(line,"GTA health loss %.1f -> %.1f weapon-damage=%d state=%d menu=%d request=%u/%u",health,ped->m_fHealth,ped->m_nLastWeaponDamage,ped->m_ePedState,weaponMenu,inventoryRequest,inventoryWeapon);Log(line);}
 previous=ped;health=ped?ped->m_fHealth:0;
}

void MaintainWorldReaction();
void BeginWorldReaction(){
 reactionWanted=FindPlayerWanted();if(!reactionWanted)return;
 savedMaximumWanted=CWanted::MaximumWantedLevel;
 changedWantedCap=savedMaximumWanted==0;
 if(changedWantedCap)CWanted::SetMaximumWantedLevel(6);
 savedPoliceBackOff=reactionWanted->m_bPoliceBackOff;
 savedEverybodyBackOff=reactionWanted->m_bEverybodyBackOff;
 savedLeaveAlone=reactionWanted->m_bLeavePlayerAlone;
 savedGarageBackOff=reactionWanted->m_bPoliceBackOffGarage;savedCrimeMultiplier=reactionWanted->m_fMultiplier;
 reactionWanted->m_bPoliceBackOff=false;reactionWanted->m_bEverybodyBackOff=false;reactionWanted->m_bLeavePlayerAlone=false;
 MaintainWorldReaction();
 char line[180];sprintf_s(line,"World reaction enabled: wanted-cap=%u old-cap=%u backoff=%d/%d/%d",CWanted::MaximumWantedLevel,savedMaximumWanted,savedPoliceBackOff,savedEverybodyBackOff,savedLeaveAlone);Log(line);
 // async; never stall the stable player loop
 for(int model:{342,343,335,363,C4Model})CStreaming::RequestModel(model,2);
}
void MaintainWorldReaction(){
 if(!active||!reactionWanted||reactionWanted!=FindPlayerWanted())return;
 // Peaceful world (F7): police and NPCs leave the player alone and MW2
 // combat is not reported as crime. Restored with the rest on exit.
 if(peacefulWorld){
  reactionWanted->m_bPoliceBackOff=true;reactionWanted->m_bEverybodyBackOff=true;reactionWanted->m_bLeavePlayerAlone=true;
  if(CWanted::MaximumWantedLevel!=0){CWanted::SetMaximumWantedLevel(0);changedWantedCap=true;}
  if(reactionWanted->m_nWantedLevel)if(auto* ped=FindPlayerPed())ped->SetWantedLevel(0);
  return;
 }
 // Mission scripts can reassert these after activation. Keep native pursuit
 // available while IW4 owns the player; restore the mission state on exit.
 bool changed=reactionWanted->m_bPoliceBackOff||reactionWanted->m_bEverybodyBackOff||reactionWanted->m_bLeavePlayerAlone||reactionWanted->m_bPoliceBackOffGarage;
 reactionWanted->m_bPoliceBackOff=false;reactionWanted->m_bEverybodyBackOff=false;reactionWanted->m_bLeavePlayerAlone=false;reactionWanted->m_bPoliceBackOffGarage=false;
 if(CWanted::MaximumWantedLevel==0){CWanted::SetMaximumWantedLevel(6);changedWantedCap=true;changed=true;}
 if(!std::isfinite(reactionWanted->m_fMultiplier)||reactionWanted->m_fMultiplier<=0)reactionWanted->m_fMultiplier=1.0f;
 if(changed)reactionWanted->UpdateWantedLevel();
}
void RestoreWorldReaction(){
 if(changedWantedCap&&(CWanted::MaximumWantedLevel==6||CWanted::MaximumWantedLevel==0))CWanted::SetMaximumWantedLevel(savedMaximumWanted);
 if(reactionWanted&&reactionWanted==FindPlayerWanted()){
  reactionWanted->m_bPoliceBackOff=savedPoliceBackOff;
  reactionWanted->m_bEverybodyBackOff=savedEverybodyBackOff;
  reactionWanted->m_bLeavePlayerAlone=savedLeaveAlone;
  reactionWanted->m_bPoliceBackOffGarage=savedGarageBackOff;reactionWanted->m_fMultiplier=savedCrimeMultiplier;
  reactionWanted->UpdateWantedLevel();
 }
 reactionWanted=nullptr;changedWantedCap=false;
}
void ReportCrime(eCrimeType crime,CEntity* victim,CPed* player){
 if(player&&!peacefulWorld)reinterpret_cast<void(__cdecl*)(eCrimeType,CEntity*,CPed*)>(0x532010)(crime,victim,player);
}
void NotifyGunshot(CPed* player,CVector start,CVector end){
 if(!player||peacefulWorld)return;
 ReportCrime(CRIME_FIRE_WEAPON,player,player);
 // Construct and clone the original event exactly as CWeapon does. Calling
 // its destructor with flags=0 releases references without freeing this stack.
 alignas(4) std::array<uint8_t,0x34> event{};
 reinterpret_cast<void*(__thiscall*)(void*,CEntity*,CVector,CVector,bool)>(0x4AC610)(event.data(),player,start,end,false);
 auto* group=reinterpret_cast<void*(__cdecl*)()>(0x4ABA50)();
 if(group)reinterpret_cast<void*(__thiscall*)(void*,void*,bool)>(0x4AB420)(group,event.data(),false);
 auto** table=*reinterpret_cast<void***>(event.data());
 reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(table[0])(event.data(),0);
}
void DeleteGrenade(GrenadeProxy& proxy){
 if(proxy.reference)if(auto* object=CPools::GetObject(proxy.reference)){CWorld::Remove(object);delete object;}
 proxy={};
}
void ClearGrenades(){for(auto& proxy:grenadeProxies)DeleteGrenade(proxy);grenadeCollisionCenters.clear();}
const char* CatalogName(uint32_t weapon){for(const auto& entry:weaponCatalog)if(entry.weapon==weapon)return entry.name;return "";}
bool StunGrenade(uint32_t id,uint32_t weapon,CVector p,CPed* player);
void MaintainStuns();
// A thrown C4 is waiting for its detonator.
bool c4Placed=false;
// Closest GTA model for each MW2 projectile; the frag grenade is the default.
// The satchel charge stands in for the claymore.
int ProxyModel(uint32_t weapon){
 const char* name=CatalogName(weapon);
 if(!strcmp(name,"throwingknife"))return 335;
 if(strstr(name,"flash")||strstr(name,"concussion")||strstr(name,"smoke"))return 343;
 if(!strcmp(name,"c4"))return C4Model;
 if(!strcmp(name,"claymore"))return 363;
 return 342;
}
// MW2 smoke: GTA's tear-gas effect, visual only, re-emitted around the
// landing point for the cloud's lifetime. Each puff is killed explicitly.
struct SmokeCloud {CVector origin;DWORD started=0,lastPuff=0;};
std::vector<SmokeCloud> smokeClouds;
std::vector<std::pair<FxSystem_c*,DWORD>> smokePuffs;
void UpdateSmoke(){
 DWORD now=GetTickCount();
 for(auto& puff:smokePuffs)if(puff.first&&now>=puff.second){puff.first->Kill();puff.first=nullptr;}
 smokePuffs.erase(std::remove_if(smokePuffs.begin(),smokePuffs.end(),[](const auto& p){return !p.first;}),smokePuffs.end());
 smokeClouds.erase(std::remove_if(smokeClouds.begin(),smokeClouds.end(),[now](const SmokeCloud& c){return now-c.started>40000;}),smokeClouds.end());
 for(auto& cloud:smokeClouds){
  if(now-cloud.lastPuff<1500)continue;cloud.lastPuff=now;
  static const float offsets[5][2]={{0,0},{1.6f,0},{-1.6f,0},{0,1.6f},{0,-1.6f}};
  for(const auto& o:offsets){
   RwV3d at{cloud.origin.x+o[0],cloud.origin.y+o[1],cloud.origin.z+.3f};
   static char name[]="teargas";
   if(auto* fx=g_fxMan.CreateFxSystem(name,&at,nullptr,true)){fx->Play();smokePuffs.emplace_back(fx,now+3500);}
  }
 }
}
void ClearSmoke(){for(auto& puff:smokePuffs)if(puff.first)puff.first->Kill();smokePuffs.clear();smokeClouds.clear();}
void UpdateGrenades(const Projectile* rows,uint32_t count){
 if(!active){ClearGrenades();ClearSmoke();return;}
 grenadeCollisionCenters.clear();
 for(auto& proxy:grenadeProxies){
  bool exists=false;for(uint32_t i=0;i<count;i++)if(rows[i].id==proxy.id)exists=true;
  if(exists||!proxy.id)continue;
  if(proxy.weapon)StunGrenade(proxy.id,proxy.weapon,proxy.position,FindPlayerPed());
  DeleteGrenade(proxy);
 }
 MaintainStuns();
 c4Placed=false;
 for(uint32_t i=0;i<count;i++)if(rows[i].id&&!strcmp(CatalogName(rows[i].weapon),"c4"))c4Placed=true;
 for(uint32_t i=0;i<count;i++){
  const auto& row=rows[i];if(!row.id||GetTickCount()-row.tick>500)continue;
  CVector p(row.origin[0],row.origin[1],row.origin[2]);if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))continue;
  grenadeCollisionCenters.push_back(p);
  GrenadeProxy* proxy=nullptr;for(auto& candidate:grenadeProxies)if(candidate.id==row.id){proxy=&candidate;break;}
  if(!proxy)for(auto& candidate:grenadeProxies)if(!candidate.id){proxy=&candidate;candidate.id=row.id;break;}
  if(!proxy)continue;
  proxy->weapon=row.weapon;proxy->position=p;
  auto* object=proxy->reference?CPools::GetObject(proxy->reference):nullptr;
  if(!object){
   int model=ProxyModel(row.weapon);
   if(CStreaming::ms_aInfoForModel[model].m_nLoadState!=1){CStreaming::RequestModel(model,2);continue;}
   object=CObject::Create(model);if(!object)continue;
   object->CreateRwObject();
   proxy->reference=CPools::GetObjectRef(object);
   object->m_nObjectType=OBJECT_MISSION;object->bUsesCollision=false;object->bApplyGravity=false;object->bDontApplySpeed=true;object->bStreamingDontDelete=true;
   object->SetPosition(p);CWorld::Add(object);
   char message[100];sprintf_s(message,"IW4 grenade visible id=%u weapon=%u",row.id,row.weapon);Log(message);
  }
  CWorld::Remove(object);object->SetPosition(p);// The dynamite model stands upright; C4 lies flat on what it sticks to.
  float lay=object->m_nModelIndex==C4Model?Pi/2:0.0f;
  object->SetOrientation(row.angles[0]*Pi/180+lay,row.angles[1]*Pi/180,row.angles[2]*Pi/180);CWorld::Add(object);object->UpdateRwMatrix();object->UpdateRwFrame();
 }
}
void ConsumeShot(const Shot& shot,CPed* player);
// IW4 knife flight never meets GTA peds. Test each frame's flight segment and
// resolve a hit through the normal accepted-shot impact path, once per knife.
void ThrowingKnifeHits(const Projectile* rows,uint32_t count,CPed* player){
 if(!active||!player)return;
 for(uint32_t i=0;i<count;i++){
  const auto& row=rows[i];if(strcmp(CatalogName(row.weapon),"throwingknife"))continue;
  GrenadeProxy* proxy=nullptr;for(auto& candidate:grenadeProxies)if(candidate.id==row.id){proxy=&candidate;break;}
  if(!proxy||proxy->triggered)continue;
  CVector p(row.origin[0],row.origin[1],row.origin[2]);
  if(!proxy->hasLast){proxy->last=p;proxy->hasLast=true;continue;}
  CVector from=proxy->last,segment=p-from;proxy->last=p;
  float length=segment.Magnitude();if(length<.01f||length>20.0f)continue;
  CColPoint hit{};CEntity* entity=nullptr;
  CEntity* saved=CWorld::pIgnoreEntity;CWorld::pIgnoreEntity=player;
  bool any=CWorld::ProcessLineOfSight(from,p+segment/length*.4f,hit,entity,false,false,true,false,false,false,false,false);
  CWorld::pIgnoreEntity=saved;
  if(!any||!entity||entity->m_nType!=ENTITY_TYPE_PED||static_cast<CPed*>(entity)->m_fHealth<=0)continue;
  proxy->triggered=true;
  Shot shot{};shot.sequence=1;shot.weapon=row.weapon;shot.hostTick=GetTickCount();
  shot.origin[0]=from.x;shot.origin[1]=from.y;shot.origin[2]=from.z;
  CVector direction=segment/length;shot.direction[0]=direction.x;shot.direction[1]=direction.y;shot.direction[2]=direction.z;
  shot.range=std::min(2.9f,length+.6f);shot.damage=shot.minDamage=135;shot.reserved[0]=1;
  Log("Throwing knife hit a GTA ped");
  ConsumeShot(shot,player);
 }
}
// Flash and concussion: no lethal blast. GTA peds with line of sight are
// blinded for a time that falls off with distance, as MW2's flash does: they
// play GTA's tear-gas reaction (hunched over, covering the face, coughing),
// which does no damage. GTA's event responses can replace it, so it is
// re-issued until the stun runs out.
struct StunnedPed {int reference=0;DWORD until=0,lastTask=0;};
std::vector<StunnedPed> stunnedPeds;
std::vector<std::pair<uint32_t,DWORD>> stunnedGrenades;
void MaintainStuns(){
 DWORD now=GetTickCount();
 stunnedPeds.erase(std::remove_if(stunnedPeds.begin(),stunnedPeds.end(),[now](StunnedPed& s){
  auto* ped=CPools::GetPed(s.reference);
  if(!ped||ped->m_fHealth<=0||ped->bInVehicle||!ped->m_pIntelligence||static_cast<int>(s.until-now)<=0)return true;
  if(now-s.lastTask>=400){
   s.lastTask=now;
   if(!ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_SIMPLE_CHOKING))ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimpleChoking(nullptr,false),TASK_PRIMARY_EVENT_RESPONSE_TEMP,false);
  }
  return false;
 }),stunnedPeds.end());
}
void StunPeds(CVector p,float radius,DWORD maxMs,CPed* player){
 std::array<CEntity*,64> nearby{};short n=0;
 CWorld::FindObjectsInRange(p,radius,false,&n,static_cast<short>(nearby.size()),nearby.data(),false,false,true,false,false);
 int stunned=0;
 for(short i=0;i<n;i++){
  auto* ped=static_cast<CPed*>(nearby[i]);if(!ped||ped==player||ped->m_fHealth<=0||ped->bInVehicle||!ped->m_pIntelligence)continue;
  auto head=ped->GetPosition()+CVector(0,0,.6f);
  if(!CWorld::GetIsLineOfSightClear(p+CVector(0,0,.2f),head,true,false,false,true,false,false,false))continue;
  float t=1.0f-std::clamp((head-p).Magnitude()/radius,0.0f,1.0f);
  int ms=static_cast<int>(1000+(maxMs-1000)*t);
  int reference=CPools::GetPedRef(ped);DWORD until=GetTickCount()+ms;
  auto known=std::find_if(stunnedPeds.begin(),stunnedPeds.end(),[reference](const StunnedPed& s){return s.reference==reference;});
  if(known==stunnedPeds.end())stunnedPeds.push_back({reference,until,0});
  else if(static_cast<int>(until-known->until)>0)known->until=until;
  stunned++;
 }
 char line[100];sprintf_s(line,"Stun grenade affected %d GTA peds",stunned);Log(line);
 MaintainStuns();
}
// Flash and concussion grenades carry no blast for GTA. Their detonation is
// the moment their projectile leaves the live list; stun once per grenade.
// The flash itself: a white light burst and GTA's camera-flash sparkle at
// the grenade, and a white-out for a player who sees it, as MW2's flash.
struct FlashLight {CVector position;DWORD until;};
std::vector<FlashLight> flashLights;
DWORD blindStart=0,blindUntil=0;
void FlashVisual(CVector p,CPed* player){
 DWORD now=GetTickCount();flashLights.push_back({p+CVector(0,0,.4f),now+200});
 static char name[]="camflash";RwV3d at{p.x,p.y,p.z+.2f};
 if(auto* fx=g_fxMan.CreateFxSystem(name,&at,nullptr,true))fx->PlayAndKill();
 if(!player||!active||!guestCamera.sequence)return;
 CVector eye(guestCamera.eye[0],guestCamera.eye[1],guestCamera.eye[2]),forward(guestCamera.forward[0],guestCamera.forward[1],guestCamera.forward[2]);
 CVector to=p+CVector(0,0,.2f)-eye;float distance=to.Magnitude();
 if(distance>20.0f||!CWorld::GetIsLineOfSightClear(eye,p+CVector(0,0,.2f),true,false,false,true,false,false,false))return;
 float facing=distance>.1f?(to.x*forward.x+to.y*forward.y+to.z*forward.z)/distance:1.0f;
 float strength=(1.0f-distance/20.0f)*(facing>.5f?1.0f:facing>0?.6f:.3f);
 if(strength<.1f)return;
 blindStart=now;blindUntil=std::max(blindUntil,now+static_cast<DWORD>(800+3200*strength));
 char line[80];sprintf_s(line,"Flashbang blinded the player for %lu ms",blindUntil-now);Log(line);
}
void UpdateFlashLights(){
 DWORD now=GetTickCount();
 flashLights.erase(std::remove_if(flashLights.begin(),flashLights.end(),[now](const FlashLight& f){return static_cast<int>(f.until-now)<=0;}),flashLights.end());
 for(const auto& f:flashLights)CPointLights::AddLight(0,f.position,CVector(0,0,0),20.0f,1.0f,1.0f,1.0f,0,false,nullptr);
}
bool StunGrenade(uint32_t id,uint32_t weapon,CVector p,CPed* player){
 const char* name=CatalogName(weapon);
 bool flash=strstr(name,"flash")!=nullptr,concussion=strstr(name,"concussion")!=nullptr;
 if(!flash&&!concussion)return false;
 // IW4 reuses projectile ids; the same id within a few seconds is the same
 // grenade reported twice (its explosion event and its disappearance).
 DWORD now=GetTickCount();
 stunnedGrenades.erase(std::remove_if(stunnedGrenades.begin(),stunnedGrenades.end(),[now](const auto& g){return now-g.second>3000;}),stunnedGrenades.end());
 for(const auto& g:stunnedGrenades)if(g.first==id)return true;
 stunnedGrenades.emplace_back(id,now);
 if(flash){FlashVisual(p,player);StunPeds(p,12.0f,5000,player);}else StunPeds(p,10.0f,3000,player);
 return true;
}
void ConsumeExplosion(const Explosion& explosion,CPed* player){
 if(!active||!player||GetTickCount()-explosion.tick>1000||!std::isfinite(explosion.radius))return;
 CVector p(explosion.origin[0],explosion.origin[1],explosion.origin[2]);
 if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
 for(auto& proxy:grenadeProxies)if(proxy.id==explosion.id)DeleteGrenade(proxy);
 // Defer the native blast by one millisecond so its radius can be taken from
 // the accepted MW2 equipment data before GTA applies damage and forces.
 int slot=-1;for(unsigned i=0;i<MAX_EXPLOSIONS;i++)if(!aExplosions[i].m_nActiveCounter){slot=static_cast<int>(i);break;}
 if(slot<0){Log("Native explosion pool full");return;}
 const char* name=CatalogName(explosion.weapon);
 if(StunGrenade(explosion.id,explosion.weapon,p,player))return;
 if(explosion.kind==2){StunPeds(p,12.0f,5000,player);return;}
 if(strstr(name,"smoke"))return;
 CExplosion::AddExplosion(nullptr,player,EXPLOSION_GRENADE,p,1,1,0.0f,0);
 auto& native=aExplosions[slot];
 native.m_fRadius=std::clamp(explosion.radius,.2f,30.0f);
 native.m_fDamagePercentage=std::clamp(explosion.innerDamage/250.0f,0.0f,4.0f);
 ReportCrime(CRIME_EXPLOSION,player,player);
 char line[180];sprintf_s(line,"IW4 grenade detonated id=%u radius=%.2f wanted=%u",explosion.id,native.m_fRadius,FindPlayerWanted()?FindPlayerWanted()->m_nWantedLevel:0);Log(line);
}
// Lock-on launchers: IW4 cannot see GTA vehicles or ground, so report what
// the guest camera aims at. Vehicles inside a narrow cone win over the ground
// point; the last aimed vehicle stays tracked so fired missiles can follow it.
int lockVehicle=0;DWORD lockVehicleSeen=0;
HostLock ComputeLock(CPed* ped){
 HostLock out{};
 if(!active||!ped||guestCamera.epoch!=hostMovement.epoch||!guestCamera.sequence)return out;
 CVector eye(guestCamera.eye[0],guestCamera.eye[1],guestCamera.eye[2]);
 CVector forward(guestCamera.forward[0],guestCamera.forward[1],guestCamera.forward[2]);
 float forwardLength=forward.Magnitude();if(forwardLength<.5f)return out;forward/=forwardLength;
 DWORD now=GetTickCount();
 if(Down(VK_RBUTTON)&&Focused()&&!weaponMenu&&!settings){
  CVehicle* best=nullptr;float bestDot=std::cos(6.0f*Pi/180);
  for(int i=0;i<CPools::ms_pVehiclePool->m_nSize;i++){
   auto* vehicle=CPools::ms_pVehiclePool->GetAt(i);if(!vehicle||vehicle->m_fHealth<=0)continue;
   auto delta=vehicle->GetPosition()-eye;float distance=delta.Magnitude();
   if(distance<8||distance>500)continue;
   float dot=(delta.x*forward.x+delta.y*forward.y+delta.z*forward.z)/distance;
   if(dot>bestDot&&CWorld::GetIsLineOfSightClear(eye,vehicle->GetPosition(),true,false,false,true,false,false,false)){best=vehicle;bestDot=dot;}
  }
  CVector point;bool vehicle=false;
  if(best){point=best->GetPosition();vehicle=true;lockVehicle=CPools::GetVehicleRef(best);}
  else{
   CColPoint hit{};CEntity* entity=nullptr;
   if(CWorld::ProcessLineOfSight(eye,eye+forward*500.0f,hit,entity,true,true,false,true,false,false,false,false)&&(hit.m_vecPoint-eye).Magnitude()>8){
    point=hit.m_vecPoint;
    if(entity&&entity->m_nType==ENTITY_TYPE_VEHICLE){vehicle=true;lockVehicle=CPools::GetVehicleRef(static_cast<CVehicle*>(entity));}
    out.flags=1;
   }
  }
  if(best||out.flags){out.flags=1|(vehicle?2u:0u);out.aim[0]=point.x;out.aim[1]=point.y;out.aim[2]=point.z;if(vehicle)lockVehicleSeen=now;}
 }
 auto* tracked=lockVehicle&&now-lockVehicleSeen<20000?CPools::GetVehicle(lockVehicle):nullptr;
 if(tracked){auto p=tracked->GetPosition();out.flags|=8;out.track[0]=p.x;out.track[1]=p.y;out.track[2]=p.z;}else lockVehicle=0;
 return out;
}
// Requests that show no menu notice: claymore triggers and infinite ammo.
void RequestSilent(uint32_t kind,uint32_t value){
 if(inventoryRequest&&featureState.ack!=inventoryRequest)return;
 ++inventoryRequest;if(!inventoryRequest)++inventoryRequest;
 inventoryKind=kind;inventoryWeapon=value;
}
void RequestDetonation(uint32_t projectile){RequestSilent(3,projectile);}
DWORD lastAutoResupply=0;
// Refill as soon as the reserve or the lethal slot is empty, so the normal
// reload always has ammunition to take.
void AutoResupply(){
 if(!infiniteAmmo||!active||!guest.ready||GetTickCount()-lastAutoResupply<1500)return;
 if(guest.stock>0&&featureState.lethalAmmo>0)return;
 // MW2 raises the detonator only once the C4 ammo is spent. Hold the refill
 // while thrown C4 waits to be detonated, unless the gun is fully empty.
 if(c4Placed&&(guest.stock>0||guest.clip>0))return;
 lastAutoResupply=GetTickCount();RequestSilent(2,0);
}
// IW4's claymore trigger only sees IW4 players. Once a planted claymore is
// armed, detonate it when a GTA ped or vehicle comes within its trigger range.
void TriggerClaymores(const Projectile* rows,uint32_t count,CPed* player){
 if(!active||!player)return;
 DWORD now=GetTickCount();
 for(uint32_t i=0;i<count;i++){
  const auto& row=rows[i];
  bool claymore=false;for(const auto& entry:weaponCatalog)if(entry.weapon==row.weapon&&!strcmp(entry.name,"claymore")){claymore=true;break;}
  if(!claymore)continue;
  GrenadeProxy* proxy=nullptr;for(auto& candidate:grenadeProxies)if(candidate.id==row.id){proxy=&candidate;break;}
  if(!proxy||proxy->triggered)continue;
  CVector p(row.origin[0],row.origin[1],row.origin[2]);
  if(!proxy->seen||(proxy->rest-p).Magnitude()>.05f){proxy->seen=now;proxy->rest=p;continue;}
  if(now-proxy->seen<1500)continue;
  std::array<CEntity*,16> nearby{};short n=0;
  CWorld::FindObjectsInRange(p,2.5f,false,&n,static_cast<short>(nearby.size()),nearby.data(),false,true,true,false,false);
  for(short j=0;j<n;j++){
   auto* entity=nearby[j];if(!entity||entity==player)continue;
   if(entity->m_nType==ENTITY_TYPE_PED&&static_cast<CPed*>(entity)->m_fHealth<=0)continue;
   if(std::abs(entity->GetPosition().z-p.z)>2.0f)continue;
   proxy->triggered=true;RequestDetonation(row.id);
   char line[120];sprintf_s(line,"Claymore %u triggered by GTA %s",row.id,entity->m_nType==ENTITY_TYPE_PED?"ped":"vehicle");Log(line);
   break;
  }
 }
}
// A smoke grenade starts its cloud once it has come to rest for a second.
void TriggerSmoke(const Projectile* rows,uint32_t count){
 if(!active)return;
 DWORD now=GetTickCount();
 for(uint32_t i=0;i<count;i++){
  const auto& row=rows[i];if(!strstr(CatalogName(row.weapon),"smoke"))continue;
  GrenadeProxy* proxy=nullptr;for(auto& candidate:grenadeProxies)if(candidate.id==row.id){proxy=&candidate;break;}
  if(!proxy||proxy->triggered)continue;
  CVector p(row.origin[0],row.origin[1],row.origin[2]);
  if(!proxy->seen||(proxy->rest-p).Magnitude()>.05f){proxy->seen=now;proxy->rest=p;continue;}
  if(now-proxy->seen<1000)continue;
  proxy->triggered=true;smokeClouds.push_back({p,now,0});Log("Smoke grenade cloud started");
 }
 UpdateSmoke();
}
void RequestInventory(uint32_t kind,uint32_t weapon=0){
 if(inventoryRequest&&featureState.ack!=inventoryRequest){inventoryNotice="Inventory request pending";inventoryNoticeUntil=GetTickCount()+2000;return;}
 ++inventoryRequest;if(!inventoryRequest)++inventoryRequest;
 inventoryKind=kind;inventoryWeapon=weapon;inventoryNotice="Inventory request pending";inventoryNoticeUntil=GetTickCount()+3000;
}
void InventoryInput(){
 // Poll on every active frame, including frames with the menu closed. An
 // Enter release outside the menu must reset its edge for the next weapon.
 bool up=Edge(VK_UP),down=Edge(VK_DOWN),left=Edge(VK_LEFT),right=Edge(VK_RIGHT),accept=Edge(VK_RETURN);
 if(menuAcceptHeld&&!Down(VK_RETURN))menuAcceptHeld=false;
 if(Edge(VK_F8)){weaponMenu=!weaponMenu;settings=false;selectedWeapon=std::min(selectedWeapon,weaponCatalog.empty()?size_t(0):weaponCatalog.size()-1);}
 if(Edge(VK_F9))RequestInventory(2);
 if(weaponMenu&&!weaponCatalog.empty()){
  if(up)selectedWeapon=(selectedWeapon+weaponCatalog.size()-1)%weaponCatalog.size();
  if(down)selectedWeapon=(selectedWeapon+1)%weaponCatalog.size();
  if(left)selectedWeapon=selectedWeapon>=10?selectedWeapon-10:0;
  if(right)selectedWeapon=std::min(selectedWeapon+10,weaponCatalog.size()-1);
  if(accept){menuAcceptHeld=true;RequestInventory(1,weaponCatalog[selectedWeapon].weapon);weaponMenu=false;}
 }
 if(inventoryRequest&&featureState.ack==inventoryRequest&&inventoryNotice=="Inventory request pending"){
  inventoryNotice=featureState.outcome==1?(inventoryKind==2?"Ammo replenished - R reload":!pickupNotice.empty()?pickupNotice:"Weapon equipped"):"Inventory request refused";pickupNotice.clear();inventoryNoticeUntil=GetTickCount()+3500;
 }
}
// GTA weapon pickups and Ammu-Nation purchases give the matching MW2 gun
// (or equipment); a gun already owned is refilled. A slot that gains a new
// weapon type or more ammunition counts as picked up.
struct PickupMapping {int type;const char* names[2];};
constexpr PickupMapping PickupMappings[]={
 {22,{"beretta"}},{23,{"usp silencer","usp"}},{24,{"deserteagle"}},
 {25,{"spas12"}},{26,{"ranger"}},{27,{"m1014"}},
 {28,{"uzi"}},{29,{"mp5k"}},{32,{"tmp"}},
 {30,{"ak47"}},{31,{"m4"}},
 {33,{"m21"}},{34,{"cheytac"}},
 {35,{"rpg"}},{36,{"stinger"}},{38,{"m240"}},
 {16,{"frag grenade"}},{17,{"smoke grenade"}},{39,{"c4"}},
};
const CatalogEntry* PickupWeapon(int type){
 for(const auto& m:PickupMappings){
  if(m.type!=type)continue;
  for(const char* name:m.names)if(name)for(const auto& entry:weaponCatalog)if(!strcmp(entry.name,name))return &entry;
 }
 return nullptr;
}
std::array<int,13> pickupType{};std::array<unsigned,13> pickupAmmo{};CPed* pickupPed=nullptr;
std::vector<uint32_t> pickupQueue;
void TrackPickups(CPed* ped){
 bool ready=requested&&guest.ready&&GetTickCount()-guest.tick<1500&&!weaponCatalog.empty();
 if(!ped||!ready){pickupPed=nullptr;pickupQueue.clear();return;}
 bool first=pickupPed!=ped;pickupPed=ped;
 for(int i=0;i<13;i++){
  int type=ped->m_aWeapons[i].m_eWeaponType;unsigned ammo=ped->m_aWeapons[i].m_nAmmoTotal;
  bool gained=type!=WEAPONTYPE_UNARMED&&(type!=pickupType[i]||ammo>pickupAmmo[i]);
  pickupType[i]=type;pickupAmmo[i]=ammo;
  if(first||!gained)continue;
  if(const auto* entry=PickupWeapon(type)){
   if(std::find(pickupQueue.begin(),pickupQueue.end(),entry->weapon)==pickupQueue.end())pickupQueue.push_back(entry->weapon);
   char line[120];sprintf_s(line,"GTA pickup: weapon type %d -> MW2 %s",type,entry->name);Log(line);
  }
 }
 // One request at a time, while MW2 mode is on and no other request waits.
 if(pickupQueue.empty()||!active||weaponMenu||(inventoryRequest&&featureState.ack!=inventoryRequest))return;
 uint32_t weapon=pickupQueue.front();pickupQueue.erase(pickupQueue.begin());
 RequestInventory(1,weapon);
 pickupNotice=std::string("Picked up ")+CatalogName(weapon);
}
// GTA collects a weapon pickup only on foot. While MW2 mode is on, weapon
// pickups update as if the player were in a vehicle, so walking over one
// does nothing; E takes it (TakeWeaponPickup) and the tracker above gives the
// MW2 gun. Other pickups (health, armour, money...) are unchanged.
bool GatedPickup(const CPickup& p){
 switch(p.m_nPickupType){
  case PICKUP_ON_STREET:case PICKUP_ON_STREET_SLOW:case PICKUP_ONCE:case PICKUP_ONCE_TIMEOUT:case PICKUP_ONCE_TIMEOUT_SLOW:case PICKUP_ONCE_FOR_MISSION:break;
  default:return false;
 }
 return p.m_pObject&&!p.m_nFlags.bDisabled&&PickupWeapon(CPickups::WeaponForModel(p.m_nModelIndex));
}
// These pickup types never read the vehicle; it only has to be non-null.
alignas(16) char notOnFoot[0x1000]{};
bool __fastcall UpdatePickup(CPickup* self,void*,CPlayerPed* player,CVehicle* vehicle,int id){
 if(active&&!vehicle&&GatedPickup(*self))vehicle=reinterpret_cast<CVehicle*>(notOnFoot);
 return plugin::CallMethodAndReturn<bool,0x457410,CPickup*,CPlayerPed*,CVehicle*,int>(self,player,vehicle,id);
}
std::string pickupPrompt;
void TakeWeaponPickup(CPed* ped,CPickup& p){
 auto type=static_cast<eWeaponType>(CPickups::WeaponForModel(p.m_nModelIndex));
 unsigned ammo=p.m_nAmmo;
 if(!ammo&&!p.m_nFlags.bEmpty)if(auto* info=CWeaponInfo::GetWeaponInfo(type,1))ammo=std::max(1u,static_cast<unsigned>(info->m_nAmmoClip)*3u);
 ped->GiveWeapon(type,ammo,false);
 AudioEngine.ReportFrontendAudioEvent(AE_FRONTEND_PICKUP_WEAPON,0.0f,1.0f);
 // As GTA does: street pickups come back after 30 s, the others are gone.
 if(p.m_nPickupType==PICKUP_ON_STREET||p.m_nPickupType==PICKUP_ON_STREET_SLOW){
  p.m_nRegenerationTime=CTimer::m_snTimeInMilliseconds+(p.m_nPickupType==PICKUP_ON_STREET?30000:36000);
  p.GetRidOfObjects();p.m_nFlags.bDisabled=1;
 }else p.Remove();
 char line[100];sprintf_s(line,"Weapon pickup taken with E: GTA type %d",type);Log(line);
}
void WeaponPickupInput(CPed* ped){
 bool take=Edge('E');pickupPrompt.clear();
 if(!active||!ped||weaponMenu||settings)return;
 CPickup* nearest=nullptr;float best=1.6f;
 for(unsigned i=0;i<MAX_NUM_PICKUPS;i++){
  auto& p=CPickups::aPickUps[i];if(!GatedPickup(p))continue;
  CVector d=p.m_pObject->GetPosition()-ped->GetPosition();float xy=std::sqrt(d.x*d.x+d.y*d.y);
  if(std::abs(d.z)<2.0f&&xy<best){best=xy;nearest=&p;}
 }
 if(!nearest)return;
 if(take&&Focused()){TakeWeaponPickup(ped,*nearest);return;}
 pickupPrompt=std::string("Press E to pick up ")+PickupWeapon(CPickups::WeaponForModel(nearest->m_nModelIndex))->name;
}
// MW2 announcer lines for GTA events; the ids match the runtime's
// gta_bridge/announcer.rs LINES. Only the newest request plays.
enum AnnouncerLine:uint32_t{EnemyUav=1,EnemyHeli=2,MissionSuccess=3,MissionFail=4,AchieveUav=5,UseUav=6,AchieveCarePackage=7,UseCarePackage=8,AchievePredator=9,UsePredator=10};
void Announce(uint32_t line){if(!announcerOn||announcerVolume<.05f)return;++announceSequence;if(!announceSequence)++announceSequence;announceLine=line;}
// The police as an enemy team: a new wanted level is an enemy UAV, the
// police helicopter is an enemy chopper, losing the police is a success and
// being wasted or busted a failure.
void PoliceAnnouncer(CPed* ped){
 static uint32_t lastWanted=0;static int lastState=PLAYERSTATE_PLAYING;static DWORD lastUav=0,lastHeli=0,lastFail=0;static bool heliSeen=false;
 auto* wanted=FindPlayerWanted();
 int state=CWorld::Players[0].m_nPlayerState;uint32_t level=wanted?wanted->m_nWantedLevel:0;
 bool ready=requested&&guest.ready&&GetTickCount()-guest.tick<1500&&ped&&wanted;
 if(!ready){lastWanted=level;lastState=state;heliSeen=false;return;}
 DWORD now=GetTickCount();
 if(state!=lastState&&(state==PLAYERSTATE_HASDIED||state==PLAYERSTATE_HASBEENARRESTED)){Announce(MissionFail);lastFail=now;}
 else if(state==PLAYERSTATE_PLAYING){
  if(level>0&&lastWanted==0){if(now-lastUav>20000){Announce(EnemyUav);lastUav=now;}}
  else if(level==0&&lastWanted>0){if(now-lastFail>20000)Announce(MissionSuccess);}
  else if(level>0&&!heliSeen&&now-lastHeli>60000){
   auto* pool=CPools::ms_pVehiclePool;
   for(int i=0;pool&&i<pool->m_nSize;i++){
    auto* v=pool->GetAt(i);if(!v||v->m_nModelIndex!=MODEL_POLMAV)continue;
    if((v->GetPosition()-ped->GetPosition()).Magnitude()>200.0f)continue;
    Announce(EnemyHeli);heliSeen=true;lastHeli=now;break;
   }
  }
 }
 if(level==0)heliSeen=false;
 lastWanted=level;lastState=state;
}
// MW2's radar on GTA's: an enemy that fires an unsilenced gun shows as a red
// dot for a few seconds, as in MW2. While a UAV is up (killstreak) every
// hostile shows: police while wanted, rival gangs and anyone who has fired.
// Grove Street members are friendly and never shown.
DWORD uavUntil=0;
struct RadarContact {int reference;DWORD fired;};
std::vector<RadarContact> radarContacts;
bool IsGun(int type){return type>=WEAPONTYPE_PISTOL&&type<=WEAPONTYPE_MINIGUN&&type!=WEAPONTYPE_FTHROWER;}
void TrackRadarContacts(CPed* player){
 if(!requested||!player){radarContacts.clear();return;}
 DWORD now=GetTickCount();
 radarContacts.erase(std::remove_if(radarContacts.begin(),radarContacts.end(),[now](const RadarContact& c){return now-c.fired>10000||!CPools::GetPed(c.reference);}),radarContacts.end());
 auto* pool=CPools::ms_pPedPool;
 for(int i=0;pool&&i<pool->m_nSize;i++){
  auto* ped=pool->GetAt(i);
  if(!ped||ped==player||ped->m_fHealth<=0||ped->m_nPedType==PED_TYPE_GANG2)continue;
  const auto& weapon=ped->m_aWeapons[ped->m_nSelectedWepSlot];
  if(weapon.m_nState!=WEAPONSTATE_FIRING||!IsGun(weapon.m_eWeaponType)||weapon.m_eWeaponType==WEAPONTYPE_PISTOL_SILENCED)continue;
  if((ped->GetPosition()-player->GetPosition()).Magnitude()>200.0f)continue;
  int reference=CPools::GetPedRef(ped);
  auto known=std::find_if(radarContacts.begin(),radarContacts.end(),[reference](const RadarContact& c){return c.reference==reference;});
  if(known==radarContacts.end())radarContacts.push_back({reference,now});else known->fired=now;
 }
}
bool UavHostile(CPed* ped){
 if(ped->m_nPedType==PED_TYPE_COP)return FindPlayerWanted()&&FindPlayerWanted()->m_nWantedLevel>0;
 return ped->m_nPedType>=PED_TYPE_GANG1&&ped->m_nPedType<=PED_TYPE_GANG10&&ped->m_nPedType!=PED_TYPE_GANG2;
}
void RadarDot(const CVector& p,const CVector& player,unsigned char alpha){
 CVector2D radar,screen;CRadar::TransformRealWorldPointToRadarSpace(radar,CVector2D(p.x,p.y));
 if(radar.x*radar.x+radar.y*radar.y>1.0f)return;
 CRadar::TransformRadarPointToScreenSpace(screen,radar);
 float dz=p.z-player.z;
 CRadar::ShowRadarTraceWithHeight(screen.x,screen.y,1,255,30,30,alpha,dz>2.0f?RADAR_TRACE_LOW:dz>=-4.0f?RADAR_TRACE_NORMAL:RADAR_TRACE_HIGH);
}
void DrawRadarContacts(){
 auto* player=FindPlayerPed();
 if(!requested||!player||FrontEndMenuManager.m_bDrawRadarOrMap)return;
 DWORD now=GetTickCount();CVector me=player->GetPosition();
 bool uav=static_cast<int>(uavUntil-now)>0;
 if(uav){
  auto* pool=CPools::ms_pPedPool;
  for(int i=0;pool&&i<pool->m_nSize;i++){
   auto* ped=pool->GetAt(i);
   if(!ped||ped==player||ped->m_fHealth<=0)continue;
   bool fired=std::any_of(radarContacts.begin(),radarContacts.end(),[ped](const RadarContact& c){return CPools::GetPed(c.reference)==ped;});
   if(fired||UavHostile(ped))RadarDot(ped->GetPosition(),me,255);
  }
  return;
 }
 // A shot pings for three seconds and fades, as MW2's radar does.
 for(const auto& c:radarContacts){
  DWORD age=now-c.fired;if(age>3000)continue;
  if(auto* ped=CPools::GetPed(c.reference))RadarDot(ped->GetPosition(),me,static_cast<unsigned char>(255-age*200/3000));
 }
}
// MW2 killstreaks for GTA kills, MW2's default set at twice its cost for
// GTA's crowds: 6 kills UAV, 8 Care Package, 10 Predator Missile. As in MW2
// each is earned once per life: after the last one no more are earned until
// GTA death or arrest resets the count. Earned streaks are kept until used,
// each with its own key (4, 5, 6).
int killStreak=0;bool killsSeeded=false;
struct StreakSplash {std::string title,line1,line2;DWORD started=0;} streakSplash;
std::vector<int> countedKills,earnedStreaks;
enum Killstreak{StreakUav=1,StreakCarePackage=2,StreakPredator=3};
const char* StreakName(int s){return s==StreakUav?"UAV":s==StreakCarePackage?"Care Package":"Predator Missile";}
// Each reward has its own key, as MW2's killstreak slots: 4, 5 and 6.
char StreakKey(int s){return static_cast<char>('3'+s);}
void CountKills(CPed* player){
 if(!requested||!player){killsSeeded=false;countedKills.clear();return;}
 if(CWorld::Players[0].m_nPlayerState!=PLAYERSTATE_PLAYING)killStreak=0;
 countedKills.erase(std::remove_if(countedKills.begin(),countedKills.end(),[](int r){return !CPools::GetPed(r);}),countedKills.end());
 auto* pool=CPools::ms_pPedPool;
 for(int i=0;pool&&i<pool->m_nSize;i++){
  auto* ped=pool->GetAt(i);if(!ped||ped==player||ped->m_fHealth>0)continue;
  int reference=CPools::GetPedRef(ped);
  if(std::find(countedKills.begin(),countedKills.end(),reference)!=countedKills.end())continue;
  countedKills.push_back(reference);
  // Bodies already down when MW2 starts are not new kills.
  if(!killsSeeded||ped->m_pLastEntityDamage!=player)continue;
  ++killStreak;
  int earned=killStreak==6?StreakUav:killStreak==8?StreakCarePackage:killStreak==10?StreakPredator:0;
  if(!earned)continue;
  earnedStreaks.push_back(earned);
  Announce(earned==StreakUav?AchieveUav:earned==StreakCarePackage?AchieveCarePackage:AchievePredator);
  char count[40];sprintf_s(count,"%d KILL STREAK!",killStreak);
  streakSplash={StreakName(earned),count,std::string("Press ")+StreakKey(earned)+" for "+StreakName(earned),GetTickCount()};
  char line[80];sprintf_s(line,"Killstreak %d: earned %s",killStreak,StreakName(earned));Log(line);
 }
 killsSeeded=true;
}
// Care Package: a smoke marker where the player aims, then a crate drop of a
// GTA power weapon (taken with E, giving the MW2 gun) and body armour.
struct CareDrop {CVector position;DWORD at;eWeaponType weapon;};
std::vector<CareDrop> careDrops;
CVector AimPoint(float range){
 CVector eye(guestCamera.eye[0],guestCamera.eye[1],guestCamera.eye[2]),forward(guestCamera.forward[0],guestCamera.forward[1],guestCamera.forward[2]);
 CColPoint hit{};CEntity* entity=nullptr;
 CVector point=CWorld::ProcessLineOfSight(eye,eye+forward*range,hit,entity,true,true,false,true,false,false,false,false)?hit.m_vecPoint-forward*0.5f:eye+CVector(forward.x,forward.y,0)*6.0f;
 bool found=false;float ground=CWorld::FindGroundZFor3DCoord(point.x,point.y,point.z+2.0f,&found,nullptr);
 if(found)point.z=ground;
 return point;
}
void StartCarePackage(){
 constexpr eWeaponType Crates[]={WEAPONTYPE_M4,WEAPONTYPE_AK47,WEAPONTYPE_SNIPERRIFLE,WEAPONTYPE_RLAUNCHER,WEAPONTYPE_MINIGUN,WEAPONTYPE_SPAS12,WEAPONTYPE_DESERT_EAGLE};
 eWeaponType weapon=Crates[GetTickCount()%std::size(Crates)];
 if(auto* info=CWeaponInfo::GetWeaponInfo(weapon,1))CStreaming::RequestModel(info->m_nModelId,2);
 CVector p=AimPoint(40.0f);DWORD now=GetTickCount();
 smokeClouds.push_back({p,now,0});careDrops.push_back({p,now+4000,weapon});
 Announce(UseCarePackage);Log("Care package marker thrown");
}
void UpdateCareDrops(){
 DWORD now=GetTickCount();
 for(auto it=careDrops.begin();it!=careDrops.end();){
  if(static_cast<int>(now-it->at)<0){++it;continue;}
  CPickups::GenerateNewOne_WeaponType(it->position+CVector(0,0,.7f),it->weapon,PICKUP_ONCE,200,false,nullptr);
  CPickups::GenerateNewOne(it->position+CVector(1.2f,0,.7f),1242,PICKUP_ONCE,0,0,false,nullptr);
  AudioEngine.ReportFrontendAudioEvent(AE_FRONTEND_PICKUP_WEAPON,0.0f,1.0f);
  Log("Care package delivered");it=careDrops.erase(it);
 }
}
// Predator Missile: the camera rides a missile launched high above and behind
// the player. The mouse steers, left click boosts, and it explodes on impact.
struct Predator {bool active=false;CVector position;float yaw=0,pitch=60;DWORD started=0,last=0;} predator;
CVector PredatorForward(){float y=predator.yaw*Pi/180,p=predator.pitch*Pi/180;return CVector(std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),-std::sin(p));}
void StartPredator(CPed* player){
 float r=yaw*Pi/180;
 predator.position=player->GetPosition()-CVector(std::cos(r),std::sin(r),0)*60.0f+CVector(0,0,220.0f);
 CVector to=player->GetPosition()-predator.position;
 predator.yaw=std::atan2(to.y,to.x)*180/Pi;predator.pitch=std::atan2(-to.z,std::sqrt(to.x*to.x+to.y*to.y))*180/Pi;
 predator.active=true;predator.started=predator.last=GetTickCount();
 Announce(UsePredator);Log("Predator missile launched");
}
void EndPredator(bool explode){
 if(!predator.active)return;
 predator.active=false;
 if(explode)if(auto* player=FindPlayerPed()){
  CExplosion::AddExplosion(nullptr,player,EXPLOSION_AIRCRAFT,predator.position,0,1,-1.0f,0);
  CExplosion::AddExplosion(nullptr,player,EXPLOSION_ROCKET,predator.position+CVector(0,0,1.0f),0,0,-1.0f,0);
  Log("Predator missile impact");
 }
}
void UpdatePredator(CVector2D mouse){
 DWORD now=GetTickCount();float dt=std::clamp((now-predator.last)/1000.0f,0.0f,0.1f);predator.last=now;
 predator.yaw-=mouse.x*0.12f;predator.pitch=std::clamp(predator.pitch+(invertMouse?-mouse.y:mouse.y)*0.12f,30.0f,89.0f);
 float speed=Down(VK_LBUTTON)?150.0f:55.0f;
 CVector next=predator.position+PredatorForward()*speed*dt;
 CColPoint hit{};CEntity* entity=nullptr;
 if(CWorld::ProcessLineOfSight(predator.position,next,hit,entity,true,true,true,true,false,false,false,false)){predator.position=hit.m_vecPoint;EndPredator(true);return;}
 bool found=false;float ground=CWorld::FindGroundZFor3DCoord(next.x,next.y,next.z+2.0f,&found,nullptr);
 predator.position=next;
 if((found&&next.z<=ground)||now-predator.started>15000){if(found)predator.position.z=std::max(predator.position.z,ground);EndPredator(true);}
}
// The UAV itself: a small plane circling high above the player while the UAV
// is up, banked into its turn. It is moved by the mod every frame, with GTA
// physics and collision off, and removed when the UAV runs out.
constexpr int UavModel=593; // Dodo
CPlane* uavPlane=nullptr;float uavAngle=0;DWORD uavLast=0;
void RemoveUavPlane(){
 if(!uavPlane)return;
 if(CPools::ms_pVehiclePool&&CPools::GetVehicleRef(uavPlane)>=0){CWorld::Remove(uavPlane);delete uavPlane;}
 uavPlane=nullptr;Log("UAV plane removed");
}
void StartUavPlane(CPed* player){
 RemoveUavPlane();
 CStreaming::RequestModel(UavModel,2);CStreaming::LoadAllRequestedModels(false);
 if(CStreaming::ms_aInfoForModel[UavModel].m_nLoadState!=1){Log("UAV plane model not loaded");return;}
 uavPlane=new CPlane(UavModel,MISSION_VEHICLE);
 if(!uavPlane)return;
 uavAngle=0;uavLast=GetTickCount();
 uavPlane->SetPosn(player->GetPosition()+CVector(120.0f,0,140.0f));
 uavPlane->bUsesCollision=false;uavPlane->bApplyGravity=false;uavPlane->bDontApplySpeed=true;
 uavPlane->bDisableMoveForce=true;uavPlane->bDisableTurnForce=true;uavPlane->bDisableCollisionForce=true;
 uavPlane->bEngineOn=true;
 CWorld::Add(uavPlane);uavPlane->IsAlreadyFlying();
 Log("UAV plane launched");
}
void UpdateUavPlane(CPed* player){
 if(!uavPlane)return;
 DWORD now=GetTickCount();
 if(!requested||!player||static_cast<int>(uavUntil-now)<=0||CPools::GetVehicleRef(uavPlane)<0){RemoveUavPlane();return;}
 // About 30 m/s on a 120 m circle centred on the player, 140 m up.
 constexpr float Radius=120.0f,Height=140.0f,Speed=30.0f,Bank=20.0f*Pi/180;
 float dt=std::clamp((now-uavLast)/1000.0f,0.0f,0.1f);uavLast=now;uavAngle+=Speed/Radius*dt;
 float c=std::cos(uavAngle),s=std::sin(uavAngle);
 CVector forward(-s,c,0),inward(-c,-s,0),up=CVector(0,0,std::cos(Bank))+inward*std::sin(Bank);
 CVector right(forward.y*up.z-forward.z*up.y,forward.z*up.x-forward.x*up.z,forward.x*up.y-forward.y*up.x);
 auto& matrix=uavPlane->GetMatrix();
 matrix.right=right;matrix.up=forward;matrix.at=up;
 matrix.pos=player->GetPosition()+CVector(c*Radius,s*Radius,Height);
 uavPlane->m_vecMoveSpeed=CVector(0,0,0);uavPlane->m_vecTurnSpeed=CVector(0,0,0);
 uavPlane->UpdateRwMatrix();uavPlane->UpdateRwFrame();
}
void UseKillstreak(CPed* player,int streak){
 auto earned=std::find(earnedStreaks.begin(),earnedStreaks.end(),streak);
 if(earned==earnedStreaks.end()){inventoryNotice=std::string(StreakName(streak))+" not ready - UAV at 6 kills (4), Care Package at 8 (5), Predator at 10 (6)";inventoryNoticeUntil=GetTickCount()+3000;return;}
 earnedStreaks.erase(earned);
 if(streak==StreakUav){uavUntil=GetTickCount()+30000;Announce(UseUav);StartUavPlane(player);Log("UAV online");}
 else if(streak==StreakCarePackage)StartCarePackage();
 else StartPredator(player);
 inventoryNotice=std::string(StreakName(streak))+(streak==StreakPredator?" - mouse steers, left click boosts":" called in");inventoryNoticeUntil=GetTickCount()+3000;
}
void DrawInventory(){
 if(active&&!weaponMenu&&!settings){
  char line[200];std::string ready;
  for(int s:{StreakUav,StreakCarePackage,StreakPredator}){auto n=std::count(earnedStreaks.begin(),earnedStreaks.end(),s);if(!n)continue;ready+=ready.empty()?"":"   ";ready+=std::string(1,StreakKey(s))+": "+StreakName(s);if(n>1)ready+=" x"+std::to_string(n);}
  if(predator.active)sprintf_s(line,"PREDATOR MISSILE - mouse steers - left click boosts");
  else if(!ready.empty())sprintf_s(line,"KILL STREAK %d   -   %s",killStreak,ready.c_str());
  else if(killStreak>=10)sprintf_s(line,"KILL STREAK %d   -   all killstreaks earned",killStreak);
  else sprintf_s(line,"KILL STREAK %d   -   next: %s at %d",killStreak,killStreak<6?"UAV":killStreak<8?"Care Package":"Predator Missile",killStreak<6?6:killStreak<8?8:10);
  float s=RsGlobal.maximumHeight/600.0f*hudTextScale;
  CFont::SetScale(.55f*s,1.05f*s);CFont::SetColor(predator.active?CRGBA(255,80,60,255):CRGBA(235,235,235,230));CFont::SetOrientation(ALIGN_RIGHT);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetEdge(1);CFont::SetBackground(false,false);CFont::SetProportional(true);
  CFont::PrintString(RsGlobal.maximumWidth-15.0f,RsGlobal.maximumHeight*.74f,line);
  if(static_cast<int>(uavUntil-GetTickCount())>0){char uav[40];sprintf_s(uav,"UAV ONLINE  %lus",(uavUntil-GetTickCount())/1000+1);CFont::SetColor(CRGBA(143,191,63,255));CFont::PrintString(RsGlobal.maximumWidth-15.0f,RsGlobal.maximumHeight*.74f+26*s,uav);}
 }
 // MW2's killstreak splash: the reward's name, the streak and how to use it.
 if(streakSplash.started&&GetTickCount()-streakSplash.started<3500){
  float s=RsGlobal.maximumHeight/600.0f*hudTextScale,t=(GetTickCount()-streakSplash.started)/1000.0f;
  float pop=t<.15f?1.6f-4.0f*t:1.0f;unsigned char alpha=static_cast<unsigned char>(t>3.0f?255*(3.5f-t)/.5f:255);
  CFont::SetOrientation(ALIGN_CENTER);CFont::SetCentreSize(static_cast<float>(RsGlobal.maximumWidth));CFont::SetProportional(true);CFont::SetBackground(false,false);CFont::SetEdge(2);CFont::SetDropColor(CRGBA(0,0,0,alpha));
  CFont::SetFontStyle(FONT_MENU);CFont::SetScale(1.3f*s*pop,2.4f*s*pop);CFont::SetColor(CRGBA(255,226,120,alpha));
  CFont::PrintString(RsGlobal.maximumWidth*.5f,RsGlobal.maximumHeight*.16f,streakSplash.title.c_str());
  CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetEdge(1);CFont::SetScale(.75f*s,1.4f*s);CFont::SetColor(CRGBA(255,255,255,alpha));
  CFont::PrintString(RsGlobal.maximumWidth*.5f,RsGlobal.maximumHeight*.16f+52*s,streakSplash.line1.c_str());
  CFont::SetScale(.6f*s,1.15f*s);CFont::SetColor(CRGBA(200,200,200,alpha));
  CFont::PrintString(RsGlobal.maximumWidth*.5f,RsGlobal.maximumHeight*.16f+80*s,streakSplash.line2.c_str());
 }
 if(!pickupPrompt.empty()&&!weaponMenu){CFont::SetScale(.95f*hudTextScale*RsGlobal.maximumHeight/600.0f,1.7f*hudTextScale*RsGlobal.maximumHeight/600.0f);CFont::SetColor(CRGBA(255,255,255,255));CFont::SetOrientation(ALIGN_CENTER);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetEdge(1);CFont::SetBackground(false,false);CFont::SetProportional(true);CFont::SetCentreSize(static_cast<float>(RsGlobal.maximumWidth));CFont::PrintString(RsGlobal.maximumWidth*.5f,RsGlobal.maximumHeight*.62f,pickupPrompt.c_str());}
 if(weaponMenu){
  float w=static_cast<float>(RsGlobal.maximumWidth),h=static_cast<float>(RsGlobal.maximumHeight);
  CFont::SetScale(.55f,.9f);CFont::SetColor(CRGBA(255,255,255,255));CFont::SetOrientation(ALIGN_LEFT);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetEdge(1);CFont::SetBackground(false,false);CFont::SetProportional(true);
  CFont::PrintString(w*.21f,h*.18f,"WEAPONS - Up/Down / Left/Right pages / Enter select / F8 close");
  size_t first=selectedWeapon/10*10;
  for(size_t i=first;i<std::min(first+10,weaponCatalog.size());i++){
   CFont::SetColor(i==selectedWeapon?CRGBA(255,215,70,255):CRGBA(235,235,235,255));
   char line[100];sprintf_s(line,"%s%s",i==selectedWeapon?"> ":"  ",weaponCatalog[i].name);CFont::PrintString(w*.23f,h*.24f+static_cast<float>(i-first)*h*.047f,line);
  }
 }
 if(GetTickCount()<inventoryNoticeUntil){float s=RsGlobal.maximumHeight/600.0f*hudTextScale;CFont::SetColor(CRGBA(255,215,70,255));CFont::SetScale(.8f*s,1.45f*s);CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetEdge(1);CFont::SetProportional(true);CFont::SetBackground(false,false);CFont::SetOrientation(ALIGN_CENTER);CFont::SetCentreSize(static_cast<float>(RsGlobal.maximumWidth));CFont::PrintString(RsGlobal.maximumWidth*.5f,RsGlobal.maximumHeight*.70f,inventoryNotice.c_str());}
}
#include "ReactionFixture.h"
