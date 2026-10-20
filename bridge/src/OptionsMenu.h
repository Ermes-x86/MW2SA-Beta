#pragma once
// F7 options panel: sliders (FOV, announcer volume), on/off switches (the
// Settings list) and the MW2 resolution. Shapes and input run in the raw D3D
// pass; the text runs after GTA's device state is restored.
constexpr int OptionSliders=3;
int OptionRows(){return OptionSliders+SettingCount+1;}
struct OptionsLayout {float x,y,w,h,s,header,row,first;};
OptionsLayout LayoutOptions(float w,float h){
 OptionsLayout l{};l.s=h/600.0f;
 l.w=std::min(h*.95f,w*.9f);l.header=50*l.s;l.row=34*l.s;
 l.h=l.header+OptionRows()*l.row+40*l.s;
 l.x=(w-l.w)*.5f;l.y=(h-l.h)*.5f;l.first=l.y+l.header+6*l.s;
 return l;
}
float SliderValue(int i){return i==0?(fov-50)/60:i==1?announcerVolume/3.0f:(hudTextScale-.4f)/1.1f;}
void SetSlider(int i,float t){
 t=std::clamp(t,0.0f,1.0f);
 if(i==0)fov=50+t*60;else if(i==1)announcerVolume=std::round(t*30)/10;else hudTextScale=std::round((.4f+t*1.1f)*20)/20;
}
void SaveSlider(int i){
 if(i==0){SaveFov();return;}
 wchar_t value[16];swprintf_s(value,L"%.2f",i==1?announcerVolume:hudTextScale);WritePrivateProfileStringW(L"Bridge",i==1?L"AnnouncerVolume":L"HudTextScale",value,iniPath.c_str());
}
int optionDrag=-1;
constexpr DWORD OptionAccent=0xff8fbf3f;
void DrawOptionsShapes(IDirect3DDevice9* device,float w,float h){
 auto l=LayoutOptions(w,h);float s=l.s;
 Quad(device,l.x-6*s,l.y-6*s,l.w+12*s,l.h+12*s,0x70000000);
 Quad(device,l.x,l.y,l.w,l.h,0xec101317);
 Quad(device,l.x,l.y,l.w,l.header,0xff1b2026);
 Quad(device,l.x,l.y+l.header-3*s,l.w,3*s,OptionAccent);
 bool down=Down(VK_LBUTTON);static bool held=false;bool click=down&&!held;held=down;
 if(!down&&optionDrag>=0){SaveSlider(optionDrag);optionDrag=-1;}
 for(int i=0;i<OptionRows();i++){
  float ry=l.first+i*l.row;
  bool hover=uiX>=l.x&&uiX<=l.x+l.w&&uiY>=ry&&uiY<ry+l.row;
  if(hover||optionDrag==i)Quad(device,l.x+6*s,ry+2*s,l.w-12*s,l.row-4*s,0x26ffffff);
  if(i<OptionSliders){
   float tx=l.x+l.w*.46f,tw=l.w*.36f,cy=ry+l.row*.5f,t=SliderValue(i);
   Quad(device,tx,cy-3*s,tw,6*s,0xff3a4048);Quad(device,tx,cy-3*s,tw*t,6*s,OptionAccent);
   Quad(device,tx+tw*t-5*s,cy-9*s,10*s,18*s,0xfff2f2f2);
   if(click&&hover)optionDrag=i;
   if(optionDrag==i)SetSlider(i,(uiX-tx)/tw);
  }else if(i<OptionSliders+SettingCount){
   const auto& option=Settings[i-OptionSliders];bool on=*option.value;
   float pw=46*s,ph=22*s,px=l.x+l.w-20*s-pw,py=ry+(l.row-ph)*.5f;
   Quad(device,px,py,pw,ph,on?0xff6aa83a:0xff454b52);
   Quad(device,on?px+pw-ph+3*s:px+3*s,py+3*s,ph-6*s,ph-6*s,0xfff2f2f2);
   if(click&&hover){*option.value=!on;WritePrivateProfileStringW(L"Bridge",option.key,!on?L"1":L"0",iniPath.c_str());}
  }else{
   float bw=l.w*.40f,bx=l.x+l.w-20*s-bw;
   Quad(device,bx,ry+5*s,bw,l.row-10*s,0xff2a3038);Quad(device,bx,ry+l.row-7*s,bw,2*s,OptionAccent);
   if(click&&hover){
    resolutionChoice=(resolutionChoice+1)%ResolutionChoices;
    wchar_t value[8];swprintf_s(value,L"%d",resolutionChoice);WritePrivateProfileStringW(L"Bridge",L"Resolution",value,iniPath.c_str());
    RequestRuntimeRestart();
   }
  }
 }
 Quad(device,uiX-s,uiY-8*s,2*s,16*s,0xffffffff);Quad(device,uiX-8*s,uiY-s,16*s,2*s,0xffffffff);
}
void OptionsFont(float s,float scale,CRGBA color,eFontAlignment align){
 CFont::SetFontStyle(FONT_SUBTITLES);CFont::SetScale(scale*.5f*s,scale*s);CFont::SetColor(color);CFont::SetOrientation(align);
 CFont::SetDropColor(CRGBA(0,0,0,220));CFont::SetEdge(1);CFont::SetProportional(true);CFont::SetBackground(false,false);
}
void DrawOptionsText(float w,float h){
 auto l=LayoutOptions(w,h);float s=l.s;
 CFont::SetWrapx(w);CFont::SetCentreSize(w);
 CFont::SetFontStyle(FONT_MENU);CFont::SetScale(.7f*s,1.3f*s);CFont::SetColor(CRGBA(240,240,240,255));CFont::SetOrientation(ALIGN_LEFT);
 CFont::SetDropColor(CRGBA(0,0,0,220));CFont::SetEdge(1);CFont::SetProportional(true);CFont::SetBackground(false,false);
 CFont::PrintString(l.x+20*s,l.y+11*s,"OPTIONS");
 OptionsFont(s,.8f,CRGBA(143,191,63,255),ALIGN_RIGHT);CFont::PrintString(l.x+l.w-20*s,l.y+17*s,"MW2-GTA  -  F7 close");
 for(int i=0;i<OptionRows();i++){
  float ry=l.first+i*l.row,ty=ry+l.row*.5f-9*s;
  const char* label=i==0?"Field of view":i==1?"Announcer volume":i==2?"Text size (killstreaks, pickups)":i<OptionSliders+SettingCount?Settings[i-OptionSliders].label:"MW2 resolution (restarts MW2)";
  OptionsFont(s,.9f,CRGBA(230,230,230,255),ALIGN_LEFT);CFont::PrintString(l.x+20*s,ty,label);
  char value[64]{};
  if(i==0)sprintf_s(value,"%.0f",fov);
  else if(i==1)sprintf_s(value,"%.0f%%",announcerVolume*100);
  else if(i==2)sprintf_s(value,"%.0f%%",hudTextScale*100);
  else if(i>=OptionSliders+SettingCount){uint32_t rw=0,rh=0;ResolutionFor(resolutionChoice,rw,rh);sprintf_s(value,"<  %s (%ux%u)  >",ResolutionLabels[resolutionChoice],rw,rh);}
  if(i<OptionSliders){OptionsFont(s,.9f,CRGBA(143,191,63,255),ALIGN_RIGHT);CFont::PrintString(l.x+l.w-20*s,ty,value);}
  else if(value[0]){OptionsFont(s,.85f,CRGBA(240,240,240,255),ALIGN_CENTER);CFont::PrintString(l.x+l.w-20*s-l.w*.20f,ty,value);}
 }
 OptionsFont(s,.75f,CRGBA(150,150,150,255),ALIGN_LEFT);
 CFont::PrintString(l.x+20*s,l.y+l.h-30*s,"Move the mouse and click a row to change it; drag the sliders");
}
