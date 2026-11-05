#ifndef P3D_HOMER_RIG_HPP
#define P3D_HOMER_RIG_HPP

#include <p3d/entity.hpp>

// Verified against the seven original Homer model skeletons. These rigs
// share the wrist/forearm/thumb joint layout, including the two-joint thumb.
// Use skeleton identity: the actor/skin name can change independently.
inline bool p3dIsHomerHandRig(tUID skeletonUID)
{
    static const tUID rigs[] = {
        tEntity::MakeUID("homer"),
        tEntity::MakeUID("h_donut"),
        tEntity::MakeUID("h_evil"),
        tEntity::MakeUID("h_fat"),
        tEntity::MakeUID("h_scuzzy"),
        tEntity::MakeUID("h_stcrobe"),
        tEntity::MakeUID("h_undrwr")
    };
    for(unsigned i=0;i<sizeof(rigs)/sizeof(rigs[0]);++i)
        if(skeletonUID==rigs[i]) return true;
    return false;
}

// These low-detail hands have a compact palm/thumb web and no weighted
// distal middle-finger joint. They need a shorter curl, not disabled input.
inline bool p3dHomerHasCompactHands(tUID skeletonUID)
{
    return skeletonUID==tEntity::MakeUID("h_fat") ||
           skeletonUID==tEntity::MakeUID("h_evil");
}

#endif
