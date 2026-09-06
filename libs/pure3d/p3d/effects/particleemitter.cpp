/*===========================================================================
    particleemitter.cpp
    28-Feb-2001 Created by Kevin Voon

    Copyright (c)2000 Radical Entertainment Ltd.
    All rights reserved.
===========================================================================*/
#include <string.h>
#include <p3d/camera.hpp>
#include <p3d/view.hpp>
#include <p3d/matrixstack.hpp>
#include <p3d/utility.hpp>
#include <p3d/error.hpp>
#include <p3d/anim/animate.hpp>
#include <p3d/anim/channel.hpp>
#include <p3d/effects/particlegenerator.hpp>
#include <p3d/effects/particleemitter.hpp>
#include <p3d/effects/particleutility.hpp>
#include <p3d/effects/particletype.hpp>
#include <p3d/effects/particlearray.hpp>
#include <p3d/effects/particleloader.hpp>
#include <pddi/pddiext.hpp>
#include <float.h>
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <vr/openxrmanager.h>
void pglSetParticleRendering(bool enabled);
#endif

//*****************************************************************************
//
// Class tBaseEmitterFactory
//
//*****************************************************************************
tBaseEmitterFactory::tBaseEmitterFactory() :
    particleType(p3dParticleSystemConstants::UNDEFINED),
    generatorType(p3dParticleSystemConstants::UNDEFINED),
    maxNumParticles(0),
    translationalCohesion(0.0f),
    rotationalCohesion(0.0f),
    zTest(true),
    zWrite(false),
    fog(false),
    infiniteLife(false),
    autoParticleAlloc(true),
    emitterAnim(NULL),
    generatorFactory(NULL),
    particlePool(NULL),
    renderModule(NULL)
{
}

tBaseEmitterFactory::~tBaseEmitterFactory()
{
    tRefCounted::Release(emitterAnim);
    tRefCounted::Release(generatorFactory);
    tRefCounted::Release(particlePool);
    tRefCounted::Release(renderModule);
}

void tBaseEmitterFactory::SetUpEmitter(tBaseEmitter* emitter)
{
    P3DASSERT(emitter);
    emitter->CopyName(this);

    P3DASSERT(generatorFactory);
    tBaseGenerator* generator = generatorFactory->CreateGenerator();

    emitter->factory = this;
    emitter->factory->AddRef();
    tRefCounted::Assign(emitter->renderModule,renderModule);
    tRefCounted::Assign(emitter->generator,generator);

    emitter->particles = new tParticleArray(particlePool);
    emitter->particles->AddRef();

    emitter->baseLookUp = baseLookUp;

    emitter->visibilityChannel = emitterAnim->GetGroupByIndex(0)->GetBoolChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::VISIBILITY_VIS);
    emitter->translationChannel = emitterAnim->GetGroupByIndex(0)->GetVectorChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::TRANSLATION_TRA);
    emitter->rotationChannel = emitterAnim->GetGroupByIndex(0)->GetRotationChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::ROTATION_ROT);
    
    emitter->ReconnectAnimations();
}

//*****************************************************************************
//
// Class tSpriteEmitterFactory
//
//*****************************************************************************
tSpriteEmitterFactory::tSpriteEmitterFactory() :
    shader(NULL),
    angleMode(p3dParticleSystemConstants::ParticleAngleMode::SPECIFIED),
    angle(0.0f),
    colourAnimMode(p3dParticleSystemConstants::ColourAnimMode::NONE),
    texAnimMode(p3dParticleSystemConstants::TextureAnimMode::NONE),
    numTexFrames(1),
    texFrameRate(1),
    texFrameCoords(NULL)
{
    particleType = p3dParticleSystemConstants::ParticleType::SPRITE;
}

tSpriteEmitterFactory::~tSpriteEmitterFactory()
{
    tRefCounted::Release(shader);
    delete [] texFrameCoords;
}

tBaseEmitter* tSpriteEmitterFactory::CreateEmitter()
{
    tSpriteEmitter* emitter = new tSpriteEmitter;
    SetUpEmitter(emitter);
    return (tBaseEmitter*)emitter;
}

tBaseEmitter* tSpriteEmitterFactory::CreateLocalEmitter()
{
    tSpriteEmitter* emitter = new tSpriteEmitter;
    emitter->local = true; // nv
    SetUpEmitter(emitter);
    return (tBaseEmitter*)emitter;
}


void tSpriteEmitterFactory::SetUpEmitter(tBaseEmitter* emitter)
{
    P3DASSERT(emitter);
    tSpriteEmitter* spriteEmitter = (tSpriteEmitter*)emitter;
    tRefCounted::Assign(spriteEmitter->shader,shader);
    
    spriteEmitter->spriteLookUp = spriteLookUp;    
    spriteEmitter->texFrameCoords = texFrameCoords;
    
    tBaseEmitterFactory::SetUpEmitter(emitter);    
}

//*****************************************************************************
//
// Class tBaseEmitter
//
//*****************************************************************************
tBaseEmitter::tBaseEmitter() :
    factory(NULL),
    generator(NULL),
    particles(NULL),
    forcedParticleAlloc(false),
    visible(true),
    frame(0.0f),
    numPartBias(1.0f),
    emissionBias(1.0f),
    lifeBias(1.0f),
    speedBias(1.0f),
    weightBias(1.0f),
    gravityBias(1.0f),
    dragBias(1.0f),
    blendRatio(0.0f),
    blendState1(0),
    blendState2(0),
    numParticlesQueued(0.0f),
    currNumParticles(DEFAULT_MAX_NUM_PARTICLES),
    currEmissionRate(DEFAULT_EMISSION_RATE),
    currLife(DEFAULT_LIFE),
    currLifeVar(DEFAULT_LIFE_VARIATION),
    currSpeed(DEFAULT_SPEED),
    currSpeedVar(DEFAULT_SPEED_VARIATION),
    currWeight(DEFAULT_WEIGHT),
    currWeightVar(DEFAULT_WEIGHT_VARIATION),
    currGravity(DEFAULT_GRAVITY),
    currDrag(DEFAULT_DRAG),
    baseLookUp(NULL),
    visibilityChannel(NULL),
    translationChannel(NULL),
    rotationChannel(NULL),
    renderModule(NULL)
{
    numParticlesChannel[0] = NULL;
    numParticlesChannel[1] = NULL;
    emissionRateChannel[0] = NULL;
    emissionRateChannel[1] = NULL;
    lifeChannel[0] = NULL;
    lifeChannel[1] = NULL;
    lifeVarChannel[0] = NULL;
    lifeVarChannel[1] = NULL;
    speedChannel[0] = NULL;
    speedChannel[1] = NULL;
    speedVarChannel[0] = NULL;
    speedVarChannel[1] = NULL;
    weightChannel[0] = NULL;
    weightChannel[1] = NULL;
    weightVarChannel[0] = NULL;
    weightVarChannel[1] = NULL;
    gravityChannel[0] = NULL;
    gravityChannel[1] = NULL;
    dragChannel[0] = NULL;
    dragChannel[1] = NULL;
    transformMatrix.Identity();
}

tBaseEmitter::~tBaseEmitter()
{
    tRefCounted::Release(factory);
    tRefCounted::Release(generator);
    tRefCounted::Release(particles);
    tRefCounted::Release(renderModule);
}

void tBaseEmitter::ResetParticles()
{
    P3DASSERT(particles);
    numParticlesQueued = 0;
    particles->KillAllParticles();
}

void tBaseEmitter::ResetAnimation()
{
    numParticlesQueued = 0;
    SetFrame(0.0f);
}

void tBaseEmitter::ResetBiases()
{
    numPartBias = 1.0f;
    emissionBias = 1.0f;
    lifeBias = 1.0f;
    speedBias = 1.0f;
    weightBias = 1.0f;
    gravityBias = 1.0f;
    dragBias = 1.0f;
}

void tBaseEmitter::SetBlendRatio(float ratio)
{
    ratio = rmt::Clamp(ratio,0.0f,1.0f);
    if (ratio == blendRatio)
        return;

    generator->SetBlendRatio(ratio);

    int numBlendStates = factory->GetEmitterAnimation()->GetNumGroups()-1;
    if (numBlendStates>0)
    {
        float temp = ratio * rmt::LtoF(numBlendStates);  //span the range of 1.0f to numBlendStates
        int newBlendState1 = rmt::FtoL(rmt::Floor(temp));
        int newBlendState2 = rmt::FtoL(rmt::Ceil(temp));
        blendRatio = temp - rmt::LtoF(newBlendState1);
        if ((blendState1!=newBlendState1)||(blendState2!=newBlendState2))
        {
            blendState1 = newBlendState1;
            blendState2 = newBlendState2;
            ReconnectAnimations();
        }
    }
}

float tBaseEmitter::GetBlendRatio() const
{
    int numBlendStates = factory->GetEmitterAnimation()->GetNumGroups()-1;
    if (numBlendStates>0)
    {
        return ((blendRatio + rmt::LtoF(blendState1))/(rmt::LtoF(numBlendStates)));
    }
    return 0.0f;
}

void tBaseEmitter::SetFrame(float f)                      
{ 
    if (f >= 0.0f) 
    {
        frame = f; 
    }
}

void tBaseEmitter::GetPosition( rmt::Vector* position ) const
{
    // Grab the first and last particles in the array
    // Grab their positions, and return the point halfway
    // between the two

    int numLiveParticles = particles->GetNumLiveParticles();
    if ( numLiveParticles < 2 )
    {
        // Not many particles, return the emitters position
        *position = transformMatrix.Row(3);
    }
    else
    {
        tBaseParticle** particleArray = particles->GetLiveParticles();
        const rmt::Vector& firstPosition = particleArray[0]->currPos;
        const rmt::Vector& secondPosition = particleArray[ numLiveParticles-1 ]->currPos;
        *position = (firstPosition + secondPosition) * 0.5f;
    }
}

// The following two functions do expensive calculations to attempt to determine
// the largest box/sphere than encapsulates the particle system. 
// The box will *not* be optimal, and will probably be too large.
// Definitely do not to use these functions every frame. 
void tBaseEmitter::ComputePreciseBoundingBox( rmt::Box3D* box )
{
    rAssert( lifeChannel[0] != NULL );
    if ( lifeChannel[0] == NULL )
        return;

    // Ok, the following attributes determine the speed of a particle
    // speed
    // speed bias
    // 
    // Iterate through the particle emitters and find the maximum
    // Distance travelled is determined by speed and life * lifebias converted to the
    // proper units
    
    // Find max life
    // Check the channels
    // Just use the first and last values of the channel
    float lifeVarStart, lifeVarEnd;
    lifeVarChannel[0]->GetValue( -FLT_MAX, &lifeVarStart );
    lifeVarChannel[0]->GetValue( FLT_MAX, &lifeVarEnd );
    float maxLifeVar = rmt::Max( lifeVarStart, lifeVarEnd );

    float lifeStart, lifeEnd;
    lifeChannel[0]->GetValue( -FLT_MAX, &lifeStart );
    lifeChannel[0]->GetValue( FLT_MAX, &lifeEnd );
    float maxLife = rmt::Max( lifeStart, lifeEnd ) + maxLifeVar;

    float speedVarStart, speedVarEnd;
    speedVarChannel[0]->GetValue( -FLT_MAX, &speedVarStart );
    speedVarChannel[0]->GetValue( FLT_MAX, &speedVarEnd );
    float maxspeedVar = rmt::Max( speedVarStart, speedVarEnd );

    float speedStart, speedEnd;
    speedChannel[0]->GetValue( -FLT_MAX, &speedStart );
    speedChannel[0]->GetValue( FLT_MAX, &speedEnd );
    float maxspeed = rmt::Max( speedStart, speedEnd ) + maxspeedVar;

    float weightVarStart, weightVarEnd;
    weightVarChannel[0]->GetValue( -FLT_MAX, &weightVarStart );
    weightVarChannel[0]->GetValue( FLT_MAX, &weightVarEnd );
    float weightVar = rmt::Max( weightVarStart, weightVarEnd );

    float weightStart, weightEnd;
    weightChannel[0]->GetValue( -FLT_MAX, &weightStart );
    weightChannel[0]->GetValue( FLT_MAX, &weightEnd );
    float weight = rmt::Max( weightStart, weightEnd ) + weightVar;
    
    float gravStart, gravEnd;
    gravityChannel[0]->GetValue( -FLT_MAX, &gravStart );
    gravityChannel[0]->GetValue( FLT_MAX, &gravEnd );
    float grav = rmt::Max( gravStart, gravEnd );
          
    float accelDown = - grav * weight;
    float maxLifeSqr = maxLife * maxLife;

    // D = Vi * t + (1/2)a*t^2
    float distDown = ( maxspeed * maxLife  ) + accelDown * maxLifeSqr * 0.5f;
    float dist = maxspeed * maxLife;

    box->low = rmt::Vector( -dist, -distDown, -dist );        
    box->high = rmt::Vector( dist, dist, dist );        
}

void tBaseEmitter::ComputePreciseBoundingSphere( rmt::Sphere* sphere  )
{
    rmt::Box3D box;
    ComputePreciseBoundingBox( &box );
    *sphere = box.GetBoundingSphere();

}

void tBaseEmitter::Update(float deltaTime, float deltaFrame, const rmt::Matrix* matrix)
{
    if (deltaFrame==0.0f)
    {
        return;
    }

    frame += deltaFrame;
  
    rmt::Matrix prevTransform = GetTransformMatrix();
    rmt::Matrix worldMatrix;
    if (matrix)
    {
        worldMatrix = *matrix;
    }
    else
    {
        worldMatrix = *p3d::platform->GetActiveContext()->GetWorldMatrix();
    }
 
    rmt::Matrix animMatrix;
    animMatrix.Identity();    

    translationChannel->GetValue(frame, &(animMatrix.Row(3)));
    rotationChannel->GetMatrix(frame, &animMatrix);

    transformMatrix.Mult( animMatrix, worldMatrix );

    rmt::Matrix currTransform = GetTransformMatrix();
      
    UpdateEmitterAttributes();
    
    numParticlesQueued += currEmissionRate*deltaFrame;    
    float numAvailParticles = rmt::Max((float)(currNumParticles-particles->GetNumLiveParticles()),0.0f);

    if (numParticlesQueued>numAvailParticles)
    {
        numParticlesQueued = numAvailParticles;
    }
    
    if (numParticlesQueued>=1.0f)
    {
        // allocate particles from particle pool if not already allocated and automatic allocation is turned on
        if (factory->GetAutomaticParticleAllocation())
        {
            particles->AllocateParticles();    
        }
        numParticlesQueued -= (float)generator->Generate(frame,rmt::FtoL(rmt::Floor(numParticlesQueued)),this);
    }

    // if no particles are available then return
    if (particles->GetNumParticles()==0)
    {
        return;
    }

    particles->UpdateParticles(deltaTime,deltaFrame,this);
    particles->RefreshParticleArray();
    particles->SetNumLiveParticles(currNumParticles);

    // release allocated particles if automatic allocation is turned on and there are no live particles
    if ((particles->GetNumLiveParticles()==0)&&(!forcedParticleAlloc))
    {
        particles->ReleaseParticles();    
    }
      
    if (factory->GetTranslationalCohesion()>0.0f)
    {
        rmt::Vector deltaTranslation(currTransform.Row(3));
        deltaTranslation.Sub(prevTransform.Row(3));
        deltaTranslation.Scale(factory->GetTranslationalCohesion());

        int numLiveParticles = particles->GetNumLiveParticles();
        tBaseParticle** liveParticles = particles->GetLiveParticles();
        for (int i = 0; i < numLiveParticles; i++)
        {
            liveParticles[i]->currPos.Add(deltaTranslation);
        }
    }

    if (factory->GetRotationalCohesion()>0.0f)
    {
        rmt::Quaternion prevRotation;
        rmt::Quaternion currRotation;
        rmt::Quaternion deltaRotation;
        rmt::Matrix tempMatrix;
        rmt::Matrix rotMatrix;

        rotMatrix.Invert(prevTransform);
        tempMatrix.Identity();
        prevRotation.BuildFromMatrix(prevTransform);
        currRotation.BuildFromMatrix(currTransform);
        deltaRotation.Slerp(prevRotation,currRotation,factory->GetRotationalCohesion());
        deltaRotation.ConvertToMatrix(&tempMatrix);
        rotMatrix.Mult(tempMatrix);

        int numLiveParticles = particles->GetNumLiveParticles();
        tBaseParticle** liveParticles = particles->GetLiveParticles();
        for (int i = 0; i < numLiveParticles; i++)
        {
            liveParticles[i]->currPos.Sub(currTransform.Row(3));
            liveParticles[i]->currPos.Rotate(rotMatrix);
            liveParticles[i]->currPos.Add(currTransform.Row(3));
        }
    }
}
    
void tBaseEmitter::SetBias(unsigned bias, float b)
{
    switch (bias)
    {
        case p3dParticleSystemConstants::EmitterBias::NUM_PARTICLES:
        {
            numPartBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::EMISSION:
        {
            emissionBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::LIFE:
        {
            lifeBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::SPEED:
        {
            speedBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::WEIGHT:
        {
            weightBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::GRAVITY:
        {
            gravityBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::DRAG:
        {
            dragBias = b;
            break;
        }
    }
}

float tBaseEmitter::GetBias(unsigned bias) const
{
    switch (bias)
    {
        case p3dParticleSystemConstants::EmitterBias::NUM_PARTICLES:
        {
            return numPartBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::EMISSION:
        {
            return emissionBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::LIFE:
        {
            return lifeBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::SPEED:
        {
            return speedBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::WEIGHT:
        {
            return weightBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::GRAVITY:
        {
            return gravityBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::DRAG:
        {
            return dragBias;
            break;
        }
        default:
        {
            return 0.0f;
            break;
        }
    }
}

void tBaseEmitter::ReconnectAnimations()
{
    const tAnimationGroup* state = factory->GetEmitterAnimation()->GetGroupByIndex(blendState1);
    if (state)
    {
        numParticlesChannel[0] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::NUMBER_PARTICLES_NUM);
        emissionRateChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::EMISSION_RATE_RAT);
        lifeChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::LIFE_LIF);
        lifeVarChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::LIFE_VAR_LIFV);
        speedChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPEED_SPD);
        speedVarChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPEED_VAR_SPDV);
        weightChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::WEIGHT_WGT);
        weightVarChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::WEIGHT_VAR_WGTV);
        gravityChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::GRAVITY_GRA);
        dragChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::DRAG_DRA);       
        if (blendState2!=blendState1)
        {
            state = factory->GetEmitterAnimation()->GetGroupByIndex(blendState2);
            numParticlesChannel[1] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::NUMBER_PARTICLES_NUM);
            emissionRateChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::EMISSION_RATE_RAT);
            lifeChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::LIFE_LIF);
            lifeVarChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::LIFE_VAR_LIFV);
            speedChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPEED_SPD);
            speedVarChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPEED_VAR_SPDV);
            weightChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::WEIGHT_WGT);
            weightVarChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::WEIGHT_VAR_WGTV);
            gravityChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::GRAVITY_GRA);
            dragChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::DRAG_DRA);       
        }
        else
        {
            numParticlesChannel[1] = NULL;
            emissionRateChannel[1] = NULL;
            lifeChannel[1] = NULL;
            lifeVarChannel[1] = NULL;
            speedChannel[1] = NULL;
            speedVarChannel[1] = NULL;
            weightChannel[1] = NULL;
            weightVarChannel[1] = NULL;
            gravityChannel[1] = NULL;
            dragChannel[1] = NULL;       
        }
    }
}

void tBaseEmitter::UpdateEmitterAttributes()
{
    if (visibilityChannel)
    {
        visibilityChannel->GetValue(frame,&visible);
    }
    if (numParticlesChannel[0])
    {
        numParticlesChannel[0]->GetValue(frame,&currNumParticles);
        currNumParticles = rmt::FtoL(((float)currNumParticles)*numPartBias);
    }
    if (emissionRateChannel[0])
    {
        emissionRateChannel[0]->GetValue(frame,&currEmissionRate);
        currEmissionRate *= emissionBias;
        currEmissionRate = currEmissionRate>0.0f ? currEmissionRate : 0.0f;
    }
    if (factory->GetInfiniteLife())
    {
        currLife = -1.0f;
        currLifeVar = 0.0f;
    }
    else
    {
        if (lifeChannel[0])
        {
            lifeChannel[0]->GetValue(frame,&currLife);
            currLife *= lifeBias;
            currLife = currLife>0.0f ? currLife : 0.0f;
        }
    }
    if (lifeVarChannel[0])
    {
        lifeVarChannel[0]->GetValue(frame,&currLifeVar);
    }
    if (speedChannel[0])
    {
        speedChannel[0]->GetValue(frame,&currSpeed);
        currSpeed *= speedBias;
    }
    if (speedVarChannel[0])
    {
        speedVarChannel[0]->GetValue(frame,&currSpeedVar);
    }
    if (weightChannel[0])
    {
        weightChannel[0]->GetValue(frame,&currWeight);
        currWeight *= weightBias;
    }
    if (weightVarChannel[0])
    {
        weightVarChannel[0]->GetValue(frame,&currWeightVar);
    }
    if (gravityChannel[0])
    {
        gravityChannel[0]->GetValue(frame,&currGravity);
        currGravity *= gravityBias;
    }
    if (dragChannel[0])
    {
        dragChannel[0]->GetValue(frame,&currDrag);
        currDrag *= dragBias;
    }

}

//*****************************************************************************
//
// Class tSpriteEmitter
//
//*****************************************************************************
tSpriteEmitter::tSpriteEmitter() :
    shader(NULL),
    sizeBias(1.0f),
    spinBias(1.0f),
    currColour(255,255,255,255),
    currColourVar(DEFAULT_COLOUR_VARIATION),
    currAlpha(DEFAULT_ALPHA),
    currAlphaVar(DEFAULT_ALPHA_VARIATION),
    currSize(DEFAULT_SIZE),
    currSizeVar(DEFAULT_SIZE_VARIATION),
    currSpin(DEFAULT_SPIN),
    currSpinVar(DEFAULT_SPIN_VARIATION),
    local(false),
    spriteLookUp(NULL),
    texFrameCoords(NULL)
{
    colourChannel[0] = NULL;
    colourChannel[1] = NULL;
    colourVarChannel[0] = NULL;
    colourVarChannel[1] = NULL;
    alphaChannel[0] = NULL;
    alphaChannel[1] = NULL;
    alphaVarChannel[0] = NULL;
    alphaVarChannel[1] = NULL;
    sizeChannel[0] = NULL;
    sizeChannel[1] = NULL;
    sizeVarChannel[0] = NULL;
    sizeVarChannel[1] = NULL;
    spinChannel[0] = NULL;
    spinChannel[1] = NULL;
    spinVarChannel[0] = NULL;
    spinVarChannel[1] = NULL;
}

tSpriteEmitter::~tSpriteEmitter()
{
    tRefCounted::Release(shader);
}


// nv: LOD hack
unsigned g_Culled = 0;
unsigned g_Drawn = 0;
bool g_ParticleLOD = false;
float g_ParticleLODDist = 30.0f;

void tSpriteEmitter::Display()
{
    if (!visible)
    {
        return;
    }

    P3DASSERT(particles);
    if (renderModule)
    {
        renderModule->RenderInit();
    }

    if (particles->GetNumLiveParticles()>0)
    {
        // nv: LOD hack
        float dist = p3d::stack->GetMatrix()->m[3][2];
        if( g_ParticleLOD && (dist > g_ParticleLODDist) )
        {
            goto bail;
        }

        int numLiveParticles = particles->GetNumLiveParticles();
        tBaseParticle** liveParticles = particles->GetLiveParticles();

        if (((tSpriteEmitterFactory*)factory)->GetAngleMode() == p3dParticleSystemConstants::ParticleAngleMode::MOTION_ALIGNED)
        {
            //update rotations during display cuz we need view matrix
            for (int i = 0; i < numLiveParticles; i++)
            {
                tSpriteParticle* current = (tSpriteParticle*)liveParticles[i];
                rmt::Vector currPos, oldPos, angleVector;
                p3d::platform->GetActiveContext()->WorldToView(current->currPos, &currPos);
                p3d::platform->GetActiveContext()->WorldToView(current->prevPos, &oldPos);
                angleVector.Sub(currPos,oldPos);
                angleVector.NormalizeSafe();
                current->spin = rmt::ATanCoords(angleVector.x,angleVector.y) + ((tSpriteEmitterFactory*)factory)->GetAngle();
            }
        }

        rmt::Matrix transform;

        if( local )
        {
            transform = *p3d::stack->GetMatrix();
        }
        else
        {
            transform = p3d::context->GetView()->GetCamera()->GetWorldToCameraMatrix();
        }

        p3d::stack->PushIdentity();
        // turn z-writing off
        bool origZWrite = p3d::pddi->GetZWrite();
        bool zWrite = factory->GetZWrite();
        if (origZWrite != zWrite)
        {
            p3d::pddi->SetZWrite(zWrite);
        }

        // turn z compare to always
        pddiCompareMode origZCompare = p3d::pddi->GetZCompare();
        if (!factory->GetZTest())
        {
            if (origZCompare!=PDDI_COMPARE_ALWAYS)
            {
                p3d::pddi->SetZCompare(PDDI_COMPARE_ALWAYS);
            }
        }

#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
        pglSetParticleRendering(true);
#endif
        pddiPrimStream* stream = p3d::pddi->BeginPrims(shader->GetShader(), PDDI_PRIM_TRIANGLES, PDDI_V_CT, 6 * numLiveParticles);

        for (int i = 0; i < numLiveParticles; i++)
        {                                              
            tSpriteParticle* current = (tSpriteParticle*)liveParticles[i];

            float x, y;
            rmt::SinCos(current->spin + rmt::PI_BY4, &y, &x);

            x *= current->size;
            y *= current->size;

            rmt::Vector currPos;
            const rmt::Vector& temp = current->currPos;
            currPos.x = transform.m[0][0]*temp.x + transform.m[1][0]*temp.y + transform.m[2][0]*temp.z + transform.m[3][0];
            currPos.y = transform.m[0][1]*temp.x + transform.m[1][1]*temp.y + transform.m[2][1]*temp.z + transform.m[3][1];
            currPos.z = transform.m[0][2]*temp.x + transform.m[1][2]*temp.y + transform.m[2][2]*temp.z + transform.m[3][2];

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame+1], 1.0f);
            stream->Coord(currPos.x+x, currPos.y+y, currPos.z);

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame], 0.0f);
            stream->Coord(currPos.x-x, currPos.y-y, currPos.z);

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame], 1.0f);
            stream->Coord(currPos.x-y, currPos.y+x, currPos.z);

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame+1], 1.0f);
            stream->Coord(currPos.x+x, currPos.y+y, currPos.z);

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame+1], 0.0f);
            stream->Coord(currPos.x+y, currPos.y-x, currPos.z);

            stream->Colour(current->colour);
            stream->UV(texFrameCoords[current->texFrame], 0.0f);
            stream->Coord(currPos.x-x, currPos.y-y, currPos.z);
        }
        p3d::pddi->EndPrims(stream);
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
        pglSetParticleRendering(false);
#endif

        p3d::stack->Pop();    

        // restore original z-compare mode
        if (!factory->GetZTest())
        {
            if (origZCompare!=PDDI_COMPARE_ALWAYS)
            {
                p3d::pddi->SetZCompare(origZCompare);
            }
        }

        // restore z-writing (if previously enabled)
        if (origZWrite!=factory->GetZWrite())
        {
            p3d::pddi->SetZWrite(origZWrite);
        }
    }

bail:
    if (renderModule)
    {
        renderModule->RenderExit();
    }
}

void tSpriteEmitter::ResetBiases()
{
    sizeBias = 1.0f;
    spinBias = 1.0f;
    tBaseEmitter::ResetBiases();
}

void tSpriteEmitter::SetBias(unsigned bias, float b)
{
    switch (bias)
    {
        case p3dParticleSystemConstants::EmitterBias::SIZE:
        {
            sizeBias = b;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::SPIN:
        {
            spinBias = b;
            break;
        }
        default:
        {
            tBaseEmitter::SetBias(bias,b);
            break;
        }
    }
}

float tSpriteEmitter::GetBias(unsigned bias) const
{
    switch (bias)
    {
        case p3dParticleSystemConstants::EmitterBias::SIZE:
        {
            return sizeBias;
            break;
        }
        case p3dParticleSystemConstants::EmitterBias::SPIN:
        {
            return spinBias;
            break;
        }
        default:
        {
            return tBaseEmitter::GetBias(bias);
            break;
        }
    }
}

void tSpriteEmitter::ReconnectAnimations()
{
    const tAnimationGroup* state = factory->GetEmitterAnimation()->GetGroupByIndex(blendState1);
    if (state)
    {
        colourChannel[0] = state->GetColourChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::COLOUR_CLR);
        colourVarChannel[0] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::COLOUR_VAR_CLRV);
        alphaChannel[0] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::ALPHA_ALP);
        alphaVarChannel[0] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::ALPHA_VAR_ALPV);
        sizeChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SIZE_SIZ);
        sizeVarChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SIZE_VAR_SIZV);
        spinChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPIN_SPI);
        spinVarChannel[0] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPIN_VAR_SPIV);
        if (blendState2!=blendState1)
        {
            state = factory->GetEmitterAnimation()->GetGroupByIndex(blendState2);
            colourChannel[1] = state->GetColourChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::COLOUR_CLR);
            colourVarChannel[1] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::COLOUR_VAR_CLRV);
            alphaChannel[1] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::ALPHA_ALP);
            alphaVarChannel[1] = state->GetIntChannel(Pure3DAnimationChannels::ParticleSystem::Emitter::ALPHA_VAR_ALPV);
            sizeChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SIZE_SIZ);
            sizeVarChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SIZE_VAR_SIZV);
            spinChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPIN_SPI);
            spinVarChannel[1] = state->GetFloat1Channel(Pure3DAnimationChannels::ParticleSystem::Emitter::SPIN_VAR_SPIV);
        }
        else
        {
            colourChannel[1] = NULL;
            colourVarChannel[1] = NULL;
            alphaChannel[1] = NULL;
            alphaVarChannel[1] = NULL;
            sizeChannel[1] = NULL;
            sizeVarChannel[1] = NULL;
            spinChannel[1] = NULL;
            spinVarChannel[1] = NULL;
        }
    }
    tBaseEmitter::ReconnectAnimations();
}

void tSpriteEmitter::UpdateEmitterAttributes()
{
    if ((((tSpriteEmitterFactory*)factory)->GetColourAnimationMode() == p3dParticleSystemConstants::ColourAnimMode::NONE) ||
        (((tSpriteEmitterFactory*)factory)->GetColourAnimationMode() == p3dParticleSystemConstants::ColourAnimMode::ALPHA))
    {
        if (colourChannel[0])
        {
            colourChannel[0]->GetValue(frame,&currColour);
        }
    }
    if (colourVarChannel[0])
    {
        colourVarChannel[0]->GetValue(frame,&currColourVar);
    }
    if ((((tSpriteEmitterFactory*)factory)->GetColourAnimationMode() == p3dParticleSystemConstants::ColourAnimMode::NONE) ||
        (((tSpriteEmitterFactory*)factory)->GetColourAnimationMode() == p3dParticleSystemConstants::ColourAnimMode::COLOUR))
    {
        if (alphaChannel[0])
        {
            alphaChannel[0]->GetValue(frame,&currAlpha);
        }
    }
    if (alphaVarChannel[0])
    {
        alphaVarChannel[0]->GetValue(frame,&currAlphaVar);
    }
    if (sizeChannel[0])
    {
        sizeChannel[0]->GetValue(frame,&currSize);
        currSize *= sizeBias;
    }
    if (sizeVarChannel[0])
    {
        sizeVarChannel[0]->GetValue(frame,&currSizeVar);
    }
    if (((tSpriteEmitterFactory*)factory)->GetAngleMode() != p3dParticleSystemConstants::ParticleAngleMode::MOTION_ALIGNED)
    {
        if (spinChannel[0])
        {
            spinChannel[0]->GetValue(frame,&currSpin);
            currSpin *= spinBias;
        }
        if (spinVarChannel[0])
        {
            spinVarChannel[0]->GetValue(frame,&currSpinVar);
        }
    }
    tBaseEmitter::UpdateEmitterAttributes();
}
