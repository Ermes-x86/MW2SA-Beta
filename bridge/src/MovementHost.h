#pragma once
// Export GTA's loaded collision volumes. IW4 owns movement, including all
// acceleration, friction, sprint, jump and stance state.
HostMovement hostMovement{};
GuestMovement guestMovement{};
HANDLE sceneMutex=nullptr;
DWORD lastScene=0;
uint32_t sceneSequence=0;
std::vector<Triangle> sceneTriangles;
bool previousGravity=true,previousDontApplySpeed=false;
bool previousCollisionForce=false,previousMoveForce=false,previousTurnForce=false;
CVector lastAppliedFoot{};
bool appliedMovement=false;
CEntity* standingIgnored=nullptr;

CVector PlayerFoot(CPed* ped){auto p=ped->GetPosition();p.z-=1.0f;return p;}
CVector WorldVertex(const CMatrix& matrix,CVector point){return matrix.pos+matrix.right*point.x+matrix.up*point.y+matrix.at*point.z;}
void AppendSurface(CVector a,CVector b,CVector c,const CVector& center,uint32_t surface){
 if(sceneTriangles.size()>=MaxTriangles)return;
 auto intersects=[&](const CVector& origin,float radius){for(int axis=0;axis<3;axis++){
  float av=(&a.x)[axis],bv=(&b.x)[axis],cv=(&c.x)[axis],p=(&origin.x)[axis];
  float r=axis==2?6.0f:radius;
  if(std::min({av,bv,cv})>p+r||std::max({av,bv,cv})<p-r)return false;
 }return true;};
 bool nearby=intersects(center,14.0f);
 if(!nearby)for(const auto& p:grenadeCollisionCenters)if(intersects(p,6.0f)){nearby=true;break;}
 if(!nearby)return;
 CVector cross((b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x));
 if(cross.Magnitude()<.00001f)return;
 Triangle t{};CVector v[3]={a,b,c};for(int i=0;i<3;i++){t.vertices[i][0]=v[i].x;t.vertices[i][1]=v[i].y;t.vertices[i][2]=v[i].z;}t.surface=surface;sceneTriangles.push_back(t);
}
void PublishCollision(CPed* ped){
 if(!sceneMutex||!ped||GetTickCount()-lastScene<80)return;
 auto center=PlayerFoot(ped);sceneTriangles.clear();
 std::array<CEntity*,512> entities{};short count=0;
 CWorld::FindObjectsKindaColliding(center,14.0f,true,&count,static_cast<short>(entities.size()),entities.data(),true,true,true,true,false);
 // Follow genuine missiles beyond the player's collision neighborhood. Do
 // not enlarge the movement scene every frame when no grenade is in flight.
 for(const auto& p:grenadeCollisionCenters){
  std::array<CEntity*,128> nearby{};short n=0;
  CWorld::FindObjectsKindaColliding(p,6.0f,false,&n,128,nearby.data(),true,true,true,true,false);
  for(short i=0;i<n&&count<entities.size();i++)if(std::find(entities.begin(),entities.begin()+count,nearby[i])==entities.begin()+count)entities[count++]=nearby[i];
 }
 static constexpr int faces[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
 for(int e=0;e<count;e++){
  auto* entity=entities[e];if(!entity||entity==ped||!entity->bUsesCollision)continue;
  auto* model=entity->GetColModel();if(!model||!model->m_pColData)continue;
  const auto& matrix=entity->GetMatrix();auto* data=model->m_pColData;
  // Bits 8..31 identify movable vehicles, peds and physics props (doors,
  // debris) so the guest can release the player if one moves into its capsule.
  // Static world geometry keeps tag 0. Bits 0..7 remain the GTA material.
  uint32_t tag=0;
  if(entity->m_nType==ENTITY_TYPE_VEHICLE||entity->m_nType==ENTITY_TYPE_PED||entity->m_nType==ENTITY_TYPE_OBJECT){
   tag=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entity)>>2)&0xFFFFFFu;if(!tag)tag=1;
  }
  auto surfaceOf=[tag](uint8_t material){return static_cast<uint32_t>(material)|(tag<<8);};
  if(data->m_pTriangles&&data->m_pVertices){for(unsigned i=0;i<data->m_nNumTriangles&&sceneTriangles.size()<MaxTriangles;i++){
   const auto& t=data->m_pTriangles[i];
   CVector a,b,c;data->GetTrianglePoint(a,t.m_nVertA);data->GetTrianglePoint(b,t.m_nVertB);data->GetTrianglePoint(c,t.m_nVertC);
   AppendSurface(WorldVertex(matrix,a),WorldVertex(matrix,b),WorldVertex(matrix,c),center,surfaceOf(t.m_nMaterial));
  }}
  if(data->m_pBoxes){for(unsigned i=0;i<data->m_nNumBoxes&&sceneTriangles.size()<MaxTriangles;i++){
   const auto& box=data->m_pBoxes[i];CVector v[8];
   for(int j=0;j<8;j++)v[j]=WorldVertex(matrix,CVector(j&1?box.m_vecMax.x:box.m_vecMin.x,j&2?box.m_vecMax.y:box.m_vecMin.y,j&4?box.m_vecMax.z:box.m_vecMin.z));
   for(const auto& f:faces)AppendSurface(v[f[0]],v[f[1]],v[f[2]],center,surfaceOf(box.m_nMaterial));
  }}
  if(data->m_pSpheres){for(unsigned i=0;i<data->m_nNumSpheres&&sceneTriangles.size()<MaxTriangles;i++){
   const auto& sphere=data->m_pSpheres[i];const float r=sphere.m_fRadius;if(r<=0||!std::isfinite(r))continue;
   // Convex surface proxy for GTA spherical volumes; triangle/box volumes
   // above retain the exact loaded geometry.
   CVector v[6];CVector offsets[6]={{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}};
   for(int j=0;j<6;j++)v[j]=WorldVertex(matrix,sphere.m_vecCenter+offsets[j]);
   for(int pole=4;pole<6;pole++)for(int x=0;x<2;x++)for(int y=2;y<4;y++)AppendSurface(v[pole],v[x],v[y],center,surfaceOf(sphere.m_nMaterial));
  }}
 }
 Lock lock(sceneMutex);if(!lock.acquired)return;
 auto* scene=reinterpret_cast<Scene*>(shared+SceneOffset);
 if(!sceneTriangles.empty())memcpy(shared+TriangleOffset,sceneTriangles.data(),sceneTriangles.size()*sizeof(Triangle));
 scene->count=static_cast<uint32_t>(sceneTriangles.size());scene->tick=GetTickCount();scene->overflow=sceneTriangles.size()==MaxTriangles||count==entities.size();
 scene->center[0]=center.x;scene->center[1]=center.y;scene->center[2]=center.z;scene->sequence=++sceneSequence;
 lastScene=GetTickCount();hostMovement.sceneSequence=sceneSequence;
}
void BeginMovement(CPed* ped){
 ++hostMovement.epoch;if(!hostMovement.epoch)++hostMovement.epoch;
 auto foot=PlayerFoot(ped);hostMovement.foot[0]=foot.x;hostMovement.foot[1]=foot.y;hostMovement.foot[2]=foot.z;
 previousGravity=ped->bApplyGravity;previousDontApplySpeed=ped->bDontApplySpeed;
 previousCollisionForce=ped->bDisableCollisionForce;previousMoveForce=ped->bDisableMoveForce;previousTurnForce=ped->bDisableTurnForce;
 ped->bApplyGravity=false;ped->bDontApplySpeed=true;
 ped->bDisableCollisionForce=true;ped->bDisableMoveForce=true;ped->bDisableTurnForce=true;
 appliedMovement=false;lastScene=0;
}
void RestoreMovement(CPed* ped){
 if(ped->m_pEntityIgnoredCollision==standingIgnored)ped->m_pEntityIgnoredCollision=nullptr;
 standingIgnored=nullptr;
 ped->bApplyGravity=previousGravity;ped->bDontApplySpeed=previousDontApplySpeed;
 ped->bDisableCollisionForce=previousCollisionForce;ped->bDisableMoveForce=previousMoveForce;ped->bDisableTurnForce=previousTurnForce;
 ped->m_vecMoveSpeed=CVector(guestMovement.velocity[0],guestMovement.velocity[1],guestMovement.velocity[2])/50.0f;
 appliedMovement=false;
}
void ApplyMovement(CPed* ped){
 if(!active||guestMovement.epoch!=hostMovement.epoch||!guestMovement.sequence)return;
 CVector foot(guestMovement.foot[0],guestMovement.foot[1],guestMovement.foot[2]);
 if(!std::isfinite(foot.x)||!std::isfinite(foot.y)||!std::isfinite(foot.z))return;
 // The genuine IW4 client already predicts this pose. GTA displays it without
 // adding a second movement integrator or extrapolating across collisions.
 // During a guest hitch keep presenting that pose. Letting GTA take over at
 // 150 ms would move CJ independently before the next prediction arrives.
 CVector destination=foot+CVector(0,0,1.0f);
 CWorld::Remove(ped);ped->SetPosition(destination);CWorld::Add(ped);
 ped->m_vecMoveSpeed=CVector(0,0,0);ped->m_vecTurnSpeed=CVector(0,0,0);
 ped->bIsStanding=guestMovement.grounded!=0;ped->bIsInTheAir=guestMovement.grounded==0;
 ped->UpdateRwMatrix();ped->UpdateRwFrame();
 // Collision force is disabled on the mirrored ped, so GTA would treat it as
 // immovable and keep shoving a car or prop it stands on. The moving roof then
 // pins IW4's capsule. IW4 already supports the player there; let GTA ignore
 // the ped/support pair instead.
 CColPoint support{};CEntity* below=nullptr;
 if(!guestMovement.grounded||!CWorld::ProcessLineOfSight(foot+CVector(0,0,.5f),foot-CVector(0,0,.5f),support,below,false,true,false,true,false,false,false,false))below=nullptr;
 if(below){ped->m_pEntityIgnoredCollision=below;standingIgnored=below;}
 else if(standingIgnored){if(ped->m_pEntityIgnoredCollision==standingIgnored)ped->m_pEntityIgnoredCollision=nullptr;standingIgnored=nullptr;}
 lastAppliedFoot=foot;appliedMovement=true;
}
