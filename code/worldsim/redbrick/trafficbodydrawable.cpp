#include <worldsim/redbrick/trafficbodydrawable.h>
#include <p3d/shader.hpp>
#include <p3d/texture.hpp>
#include <p3d/utility.hpp>
#include <debug/profiler.h>
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#endif

TrafficBodyDrawable::TrafficBodyDrawable()
{
    mBodyPropDrawable = NULL;
    mBodyShader = NULL;
    mReflectionMap = NULL;
    mDesiredColour.Set( 255, 255, 255, 255 );
    mFading = false;
    mReflectionConfigured = false;
}

TrafficBodyDrawable::~TrafficBodyDrawable()
{
    if( mBodyPropDrawable != NULL )
    {
        mBodyPropDrawable->Release();//delete mBodyPropDrawable;
        mBodyPropDrawable = NULL;
    }
    if( mBodyShader != NULL )
    {
        mBodyShader->Release();
        mBodyShader = NULL;
    }
    if( mReflectionMap != NULL )
    {
        mReflectionMap->Release();
        mReflectionMap = NULL;
    }
}
void TrafficBodyDrawable::SetBodyPropDrawable( tDrawable* drawable )
{
    tRefCounted::Assign( mBodyPropDrawable, drawable );
}
void TrafficBodyDrawable::SetBodyShader( tShader* shader )
{
    tRefCounted::Assign( mBodyShader, shader );
}

///////////////////////////////////////////////////
// Implementing tDrawable
void TrafficBodyDrawable::Display()
{
    BEGIN_PROFILE("TrafficBodyDrawable::Display")
    rAssert( mBodyPropDrawable != NULL );
    if( mBodyPropDrawable != NULL )
    {
#if defined(RAD_ANDROID)
        // Traffic assets use "simple" chassis shaders, unlike personal cars
        // whose body panels are authored as "spheremap". Promote only the
        // wrapped traffic body/door drawable to the current level EnvMap;
        // glass, wheels, lamps and billboard effects live outside this wrapper.
        if(!mReflectionConfigured && SharOpenXR::IsVrModeEnabled())
        {
            tTexture* reflectionMap=p3d::find<tTexture>("EnvMap.bmp");
            if(reflectionMap && mBodyShader)
            {
                tRefCounted::Assign(mReflectionMap,reflectionMap);
                // The wrapper knows the dedicated recolourable chassis
                // shader. Applying REFLMAP through ProcessShaders affected
                // every primitive in the prop, including wheels and trim.
                mReflectionConfigured=true;
            }
        }
#endif
        if( mBodyShader != NULL )
        {
            // display with desired colour first, then we'll go over it with a gloss
            // put the old settings back
            // The gloss pass below uses alpha testing.  Explicitly disable it
            // before the base pass instead of relying on the deferred reset at
            // the end of the previous draw.
            mBodyShader->SetInt( PDDI_SP_ALPHATEST, 0 );
            if(!mFading)
            {
                mBodyShader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_NONE );
            }
            else
            {
                mBodyShader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_ALPHA );
            }
            mBodyShader->SetColour( PDDI_SP_DIFFUSE, mDesiredColour );
            mBodyShader->SetInt( PDDI_SP_EMISSIVEALPHA, mFadeAlpha );
#if defined(RAD_ANDROID)
            if(mReflectionMap)
            {
                // Traffic textures mark bumpers/trim with near-opaque alpha so
                // the later white pass can restore them after recolouring.
                // The GLES shader uses the inverse of that mask for paint.
                mBodyShader->SetColour(PDDI_SP_ENVBLEND,pddiColour(28,28,28,0));
                mBodyShader->SetTexture(PDDI_SP_REFLMAP,mReflectionMap);
            }
#endif
            mBodyPropDrawable->Display();

            {
                pddiColour white( 255,255,255,255 );
                mBodyShader->SetColour( PDDI_SP_DIFFUSE, white );
                mBodyShader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_ALPHA );
                mBodyShader->SetInt( PDDI_SP_EMISSIVEALPHA, mFadeAlpha );
                mBodyShader->SetInt( PDDI_SP_ALPHATEST, 1 );
                mBodyShader->SetFloat( PDDI_SP_ALPHACOMPARE_THRESHOLD, (250.0f * (float(mFadeAlpha) / 255.0f)) / 255.0f );
#if defined(RAD_ANDROID)
                if(mReflectionMap)
                    // Keep the original gloss/restore pass reflection-free.
                    // Its alpha>250 pixels are bumpers and unpainted trim.
                    mBodyShader->SetTexture(PDDI_SP_REFLMAP,NULL);
#endif
                mBodyPropDrawable->Display();


                mBodyShader->SetInt( PDDI_SP_ALPHATEST, 0 );
            }
        }
        else
        {
            mBodyPropDrawable->Display();
        }
    }
    END_PROFILE("TrafficBodyDrawable::Display")
}

void TrafficBodyDrawable::ProcessShaders(ShaderCallback& callback)
{
    rAssert( mBodyPropDrawable != NULL );
    mBodyPropDrawable->ProcessShaders(callback);
}
