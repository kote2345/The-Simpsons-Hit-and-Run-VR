#include <vr/openxr_shared_hands.h>
#include <vr/vr_hand_mesh.h>
#include <vr/vr_hand_texture.h>
#include <p3d/shader.hpp>
#include <p3d/texture.hpp>
#include <p3d/utility.hpp>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>

namespace SharOpenXR
{
namespace
{
struct HandMesh { const float* positions; const float* normals; const float* uvs; int count; };
struct CharacterHands { HandMesh left,right; };
#define VR_HAND_MESH(character,side) { vr_hand_##character##_##side##_positions, vr_hand_##character##_##side##_normals, vr_hand_##character##_##side##_uvs, vr_hand_##character##_##side##_count }
const CharacterHands homerHands={VR_HAND_MESH(homer,l),VR_HAND_MESH(homer,r)};
const CharacterHands bartHands ={VR_HAND_MESH(bart,l), VR_HAND_MESH(bart,r)};
const CharacterHands lisaHands ={VR_HAND_MESH(lisa,l), VR_HAND_MESH(lisa,r)};
const CharacterHands margeHands={VR_HAND_MESH(marge,l),VR_HAND_MESH(marge,r)};
const CharacterHands apuHands  ={VR_HAND_MESH(apu,l),  VR_HAND_MESH(apu,r)};
#undef VR_HAND_MESH

pddiShader* GetHandShader()
{
    tShader* characterShader=p3d::find<tShader>("char_swatches_lit_m");
    if(characterShader) return characterShader->GetShader();
    static tShader* shader=NULL;
    static tTexture* texture=NULL;
    if(shader) return shader->GetShader();
    shader=new tShader("simple"); shader->AddRef();
    texture=new tTexture; texture->AddRef();
    if(texture->Create(vr_hand_tex_width,vr_hand_tex_height,32,8,0))
    {
        pddiLockInfo* lock=texture->Lock(0);
        if(lock&&lock->bits)
        {
            for(int y=0;y<vr_hand_tex_height;++y)
            {
                PDDI_U32* dst=reinterpret_cast<PDDI_U32*>(reinterpret_cast<unsigned char*>(lock->bits)+y*lock->pitch);
                for(int x=0;x<vr_hand_tex_width;++x)
                {
                    const unsigned char* src=&vr_hand_tex_rgba[(y*vr_hand_tex_width+x)*4];
                    dst[x]=lock->MakeColour(pddiColour(src[0],src[1],src[2],src[3]));
                }
            }
            texture->Unlock(0);
        }
        shader->SetTexture(PDDI_SP_BASETEX,texture);
    }
    shader->SetInt(PDDI_SP_ISLIT,1); shader->SetInt(PDDI_SP_SHADEMODE,PDDI_SHADE_GOURAUD);
    shader->SetInt(PDDI_SP_FILTER,PDDI_FILTER_NONE); shader->SetInt(PDDI_SP_BLENDMODE,PDDI_BLEND_NONE);
    shader->SetInt(PDDI_SP_TWOSIDED,1); shader->SetColour(PDDI_SP_AMBIENT,tColour(255,255,255));
    shader->SetColour(PDDI_SP_DIFFUSE,tColour(255,255,255));
    return shader->GetShader();
}
}

void RenderTrackedHandMeshes(const rmt::Matrix worldPoses[2],const bool valid[2])
{
    Character* player=GetCharacterManager()?GetCharacterManager()->GetCharacter(0):NULL;
    if(!player) return;
    const CharacterHands* hands=&homerHands;
    const tUID uid=player->GetUID();
    if(uid==tEntity::MakeUID("bart")) hands=&bartHands;
    else if(uid==tEntity::MakeUID("lisa")) hands=&lisaHands;
    else if(uid==tEntity::MakeUID("marge")) hands=&margeHands;
    else if(uid==tEntity::MakeUID("apu")) hands=&apuHands;
    pddiShader* shader=GetHandShader();
    for(unsigned hand=0;hand<2;++hand)
    {
        if(!valid[hand]) continue;
        const HandMesh& mesh=hand==0?hands->right:hands->left;
        const float scale=hands==&homerHands?0.80f:1.0f;
        pddiPrimStream* stream=p3d::pddi->BeginPrims(shader,PDDI_PRIM_TRIANGLES,PDDI_V_NT,mesh.count);
        if(!stream) continue;
        for(int i=0;i<mesh.count;++i)
        {
            rmt::Vector p(mesh.positions[i*3],mesh.positions[i*3+1],mesh.positions[i*3+2]); p.Scale(scale);
            rmt::Vector n(mesh.normals[i*3],mesh.normals[i*3+1],mesh.normals[i*3+2]),wp,wn;
            worldPoses[hand].Transform(p,&wp); worldPoses[hand].RotateVector(n,&wn); wn.NormalizeSafe();
            stream->UV(mesh.uvs[i*2],mesh.uvs[i*2+1]); stream->Normal(wn.x,wn.y,wn.z); stream->Coord(wp.x,wp.y,wp.z);
        }
        p3d::pddi->EndPrims(stream);
    }
}
}
