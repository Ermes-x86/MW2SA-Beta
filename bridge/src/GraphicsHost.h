#pragma once
IDirect3DPixelShader9* chromaShader=nullptr;
void CreateChromaShader(IDirect3DDevice9* device){
 if(chromaShader)return;
 using CompileFunction=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
 static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");
 if(!compiler){Log("Chroma shader compiler unavailable; using CPU fallback");return;}
 auto compile=reinterpret_cast<CompileFunction>(GetProcAddress(compiler,"D3DCompile"));if(!compile)return;
 const char* source=R"(
 sampler2D image : register(s0);
 float4 main(float2 uv:TEXCOORD0,float4 tint:COLOR0):COLOR0 {
   float3 rgb=tex2D(image,uv).rgb;
   float matte=max(0,min(rgb.r,rgb.b)-rgb.g);
   float alpha=1-matte;
   float3 foreground=saturate((rgb-float3(matte,0,matte))/max(alpha,0.00392157));
   return float4(foreground,alpha)*tint;
 }
 )";
 ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
 HRESULT result=compile(source,strlen(source),"MW2GTAChroma",nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(SUCCEEDED(result)&&code)result=device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&chromaShader);
 if(errors){Log(static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}if(code)code->Release();
 Log(SUCCEEDED(result)?"GPU chroma shader ready; raw frame transport enabled":"GPU chroma shader failed; using CPU fallback");
}
void CpuChroma(uint8_t* target,const uint8_t* source,uint32_t width){
 for(uint32_t x=0;x<width;x++){
  auto* p=source+x*4;auto* q=target+x*4;unsigned matte=std::min(p[0],p[2])>p[1]?std::min(p[0],p[2])-p[1]:0;unsigned alpha=255-matte;
  q[3]=static_cast<uint8_t>(alpha);for(int c=0;c<3;c++){unsigned contribution=c==1?p[c]:(p[c]>matte?p[c]-matte:0);q[c]=alpha?static_cast<uint8_t>(std::min(255u,(contribution*255+alpha/2)/alpha)):0;}
 }
}
