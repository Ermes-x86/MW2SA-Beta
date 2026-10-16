#pragma once
// Only the explicit test IPC request starts this fixture. Normal gameplay
// does not spawn peds, aim, fire or alter a wanted level through this code.
bool reactionFixtureRequested=false;
DWORD reactionFixtureStarted=0,reactionFixtureLastLog=0;
int fixtureCop=0,fixtureRival=0;
void RemoveFixturePed(int& reference){if(reference)if(auto* ped=CPools::GetPed(reference))CPopulation::RemovePed(ped);reference=0;}
void ProbeReactionFixture(CPed* player){
 if(reactionFixtureRequested&&active&&player){
  reactionFixtureRequested=false;RemoveFixturePed(fixtureCop);RemoveFixturePed(fixtureRival);
  CStreaming::RequestModel(280,2);CStreaming::RequestModel(102,2);CStreaming::RequestModel(346,2);CStreaming::LoadAllRequestedModels(false);
  CVector forward(guestCamera.forward[0],guestCamera.forward[1],0);if(forward.Magnitude()<.5f)forward=CVector(1,0,0);forward/=forward.Magnitude();
  CVector side(-forward.y,forward.x,0),base=player->GetPosition();
  auto spawn=[&](ePedType type,int model,CVector p){bool found=false;float z=CWorld::FindGroundZFor3DCoord(p.x,p.y,p.z+5,&found,nullptr);if(found)p.z=z+1;
   auto* ped=CPopulation::AddPed(type,model,p,false);if(!ped)return 0;ped->bStreamingDontDelete=true;ped->m_fHealth=100;ped->GiveWeapon(WEAPONTYPE_PISTOL,500,false);ped->SetCurrentWeapon(WEAPONTYPE_PISTOL);return CPools::GetPedRef(ped);};
  fixtureRival=spawn(PED_TYPE_GANG1,102,base+forward*6);
  fixtureCop=spawn(PED_TYPE_COP,280,base+forward*6+side*3);
  reactionFixtureStarted=GetTickCount();reactionFixtureLastLog=0;Log("Explicit world reaction fixture started: cop + Ballas; no forced combat tasks");
 }
 if(!reactionFixtureStarted)return;
 auto* cop=fixtureCop?CPools::GetPed(fixtureCop):nullptr;
 auto* rival=fixtureRival?CPools::GetPed(fixtureRival):nullptr;
 auto task=[](CPed* ped){if(!ped||!ped->m_pIntelligence)return -1;auto* t=ped->m_pIntelligence->m_TaskMgr.GetActiveTask();return t?static_cast<int>(t->GetId()):-1;};
 float seconds=(GetTickCount()-reactionFixtureStarted)/1000.0f;
 uint32_t values[8]={static_cast<uint32_t>(fixtureCop),static_cast<uint32_t>(fixtureRival),FindPlayerWanted()?FindPlayerWanted()->m_nWantedLevel:0,static_cast<uint32_t>(task(cop)),static_cast<uint32_t>(task(rival)),static_cast<uint32_t>(cop&&cop->bIsAimingGun),static_cast<uint32_t>(rival&&rival->bIsAimingGun),0};
 float health=player?player->m_fHealth:0;memcpy(&values[7],&health,4);
 {Lock lock(controlMutex);if(lock.acquired){memcpy(shared+4608,values,sizeof(values));if(cop)memcpy(shared+4640,&cop->GetPosition().x,12);if(rival)memcpy(shared+4652,&rival->GetPosition().x,12);}}
 if(GetTickCount()-reactionFixtureLastLog>500){char line[220];sprintf_s(line,"WORLDTEST time=%.1f wanted=%u pursuit=%u cop-task=%d rival-task=%d aim=%u/%u cj-health=%.1f rival-health=%.1f",seconds,values[2],FindPlayerWanted()?FindPlayerWanted()->m_nCopsInPursuit:0,task(cop),task(rival),values[5],values[6],health,rival?rival->m_fHealth:0);Log(line);reactionFixtureLastLog=GetTickCount();}
 if(seconds>=15){RemoveFixturePed(fixtureCop);RemoveFixturePed(fixtureRival);reactionFixtureStarted=0;Log("World reaction fixture ended; fixture peds removed");}
}
void ApplyReactionFixtureInput(CPed* player){
 if(!reactionFixtureStarted||!player)return;
 float seconds=(GetTickCount()-reactionFixtureStarted)/1000.0f;
 if(seconds>=2&&seconds<4)if(auto* rival=CPools::GetPed(fixtureRival)){
  auto delta=rival->GetPosition()+CVector(0,0,.15f)-Eye(player);
  yaw=std::atan2(delta.y,delta.x)*180/Pi;pitch=-std::atan2(delta.z,std::hypot(delta.x,delta.y))*180/Pi;
  host.yaw=yaw;host.pitch=pitch;hostMovement.reserved[2]=1;
  if(seconds>=3&&seconds<3.18f)host.buttons|=3;
 }
}
