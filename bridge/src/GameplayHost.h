#pragma once
// GTA 1.0 US adapter. IW4 owns accepted fire/equipment; GTA owns its NPC AI,
// crimes, pursuit, native effects and damage reactions.
CWanted* reactionWanted=nullptr;
unsigned savedMaximumWanted=6;
bool changedWantedCap=false,savedPoliceBackOff=false,savedEverybodyBackOff=false,savedLeaveAlone=false;
bool savedGarageBackOff=false;float savedCrimeMultiplier=1.0f;
DWORD damageFeedbackUntil=0;float damageFeedbackStrength=0;
// F7 options, saved in MW2GTA.ini.
bool showHelpText=true,infiniteAmmo=false,healthRegen=true,peacefulWorld=false,invertMouse=false;
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
// Flash and concussion: no lethal blast. GTA peds with line of sight freeze
// in place for a time that falls off with distance, as MW2's flash does.
// GTA's event responses (fleeing gunfire, reacting to the blast) replace a
// one-off stand-still task, so the stun is re-issued until it runs out.
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
   ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskSimpleStandStill(static_cast<int>(std::min<DWORD>(s.until-now,1000)),false,false,8.0f),TASK_PRIMARY_EVENT_RESPONSE_TEMP,false);
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
 if(flash)StunPeds(p,12.0f,5000,player);else StunPeds(p,10.0f,3000,player);
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
  inventoryNotice=featureState.outcome==1?(inventoryKind==2?"Ammo replenished - R reload":"Weapon equipped"):"Inventory request refused";inventoryNoticeUntil=GetTickCount()+3500;
 }
}
void DrawInventory(){
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
 if(GetTickCount()<inventoryNoticeUntil){CFont::SetColor(CRGBA(255,215,70,255));CFont::SetScale(.55f,.9f);CFont::SetOrientation(ALIGN_LEFT);CFont::PrintString(15,static_cast<float>(RsGlobal.maximumHeight)-65,inventoryNotice.c_str());}
}
#include "ReactionFixture.h"
