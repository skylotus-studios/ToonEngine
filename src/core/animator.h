// Animation playback and joint matrix computation.
//
// The Animator holds playback state (current clip, time, looping) and
// computes the final joint matrices each frame by:
//   1. Interpolating keyframes to get local joint TRS
//   2. Walking the hierarchy to build global transforms
//   3. Multiplying by inverse bind matrices

#pragma once

#include "core/animation.h"

#include <glm/glm.hpp>

#include <vector>

struct Animator {
    int   clipIndex = -1;   // index into clips vector, -1 = no animation
    float time      = 0.0f;
    bool  playing   = true;
    bool  looping   = true;

    // Output: one matrix per joint, ready for shader upload.
    // jointMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
    std::vector<glm::mat4> jointMatrices;
};

// Advance time and recompute joint matrices for the current clip.
void AnimatorUpdate(Animator& anim, const Skeleton& skeleton,
                    const std::vector<AnimationClip>& clips, float dt);

// Switch to a different clip and reset time to 0.
void AnimatorSetClip(Animator& anim, const Skeleton& skeleton, int clipIndex);
