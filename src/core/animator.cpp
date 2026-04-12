// Animation playback implementation.

#include "animator.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

// Binary-search interpolation helpers for each keyframe type.

static glm::vec3 SamplePos(const std::vector<PosKey>& keys, float t) {
    if (keys.empty()) return glm::vec3(0.0f);
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    // Find the two bracketing keys.
    size_t i = 0;
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        if (t < keys[k + 1].time) { i = k; break; }
    }
    float seg = keys[i + 1].time - keys[i].time;
    float f = (seg > 0.0f) ? (t - keys[i].time) / seg : 0.0f;
    return glm::mix(keys[i].value, keys[i + 1].value, f);
}

static glm::quat SampleRot(const std::vector<RotKey>& keys, float t) {
    if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    size_t i = 0;
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        if (t < keys[k + 1].time) { i = k; break; }
    }
    float seg = keys[i + 1].time - keys[i].time;
    float f = (seg > 0.0f) ? (t - keys[i].time) / seg : 0.0f;
    return glm::slerp(keys[i].value, keys[i + 1].value, f);
}

static glm::vec3 SampleScale(const std::vector<ScaleKey>& keys, float t) {
    if (keys.empty()) return glm::vec3(1.0f);
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    size_t i = 0;
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        if (t < keys[k + 1].time) { i = k; break; }
    }
    float seg = keys[i + 1].time - keys[i].time;
    float f = (seg > 0.0f) ? (t - keys[i].time) / seg : 0.0f;
    return glm::mix(keys[i].value, keys[i + 1].value, f);
}

// Build a TRS matrix from position, rotation, scale.
static glm::mat4 TRSMatrix(const glm::vec3& pos, const glm::quat& rot,
                            const glm::vec3& scale) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m *= glm::toMat4(rot);
    m = glm::scale(m, scale);
    return m;
}

void AnimatorUpdate(Animator& anim, const Skeleton& skeleton,
                    const std::vector<AnimationClip>& clips, float dt) {
    int jointCount = static_cast<int>(skeleton.joints.size());
    if (jointCount == 0) return;

    // Ensure output vector is sized.
    if (static_cast<int>(anim.jointMatrices.size()) != jointCount)
        anim.jointMatrices.resize(jointCount, glm::mat4(1.0f));

    // No clip selected — set identity (bind pose).
    if (anim.clipIndex < 0 ||
        anim.clipIndex >= static_cast<int>(clips.size())) {
        for (int i = 0; i < jointCount; ++i)
            anim.jointMatrices[i] = glm::mat4(1.0f);
        return;
    }

    const AnimationClip& clip = clips[anim.clipIndex];

    // Advance time.
    if (anim.playing && clip.duration > 0.0f) {
        anim.time += dt;
        if (anim.looping) {
            while (anim.time >= clip.duration)
                anim.time -= clip.duration;
        } else {
            anim.time = std::min(anim.time, clip.duration);
        }
    }

    // Evaluate local transforms per joint.  Start from bind-pose defaults
    // so un-animated joints keep their rest-pose transform.
    std::vector<glm::vec3> localPos(jointCount);
    std::vector<glm::quat> localRot(jointCount);
    std::vector<glm::vec3> localScale(jointCount);
    for (int i = 0; i < jointCount; ++i) {
        localPos[i]   = skeleton.joints[i].bindPosition;
        localRot[i]   = skeleton.joints[i].bindRotation;
        localScale[i] = skeleton.joints[i].bindScale;
    }

    // Override only the components that actually have keyframes.
    // Bones typically only animate rotation — position and scale stay at
    // bind-pose values.  Overriding empty channels with the sampler default
    // (0,0,0 for pos, identity for rot, 1,1,1 for scale) would collapse the
    // skeleton.
    for (auto& ch : clip.channels) {
        if (ch.jointIndex < 0 || ch.jointIndex >= jointCount) continue;
        if (!ch.positions.empty())
            localPos[ch.jointIndex]   = SamplePos(ch.positions, anim.time);
        if (!ch.rotations.empty())
            localRot[ch.jointIndex]   = SampleRot(ch.rotations, anim.time);
        if (!ch.scales.empty())
            localScale[ch.jointIndex] = SampleScale(ch.scales, anim.time);
    }

    // Walk hierarchy: compute global transforms.
    // glTF doesn't guarantee parent-first order, so iterate until all
    // joints are resolved.
    std::vector<glm::mat4> globalTransform(jointCount);
    std::vector<bool> computed(jointCount, false);
    int remaining = jointCount;
    while (remaining > 0) {
        int progress = 0;
        for (int i = 0; i < jointCount; ++i) {
            if (computed[i]) continue;
            int parent = skeleton.joints[i].parent;
            if (parent >= 0 && !computed[parent]) continue; // parent not ready yet
            glm::mat4 local = TRSMatrix(localPos[i], localRot[i], localScale[i]);
            globalTransform[i] = (parent >= 0)
                ? globalTransform[parent] * local
                : local;
            computed[i] = true;
            ++progress;
        }
        remaining -= progress;
        if (progress == 0) break; // cycle or orphan — avoid infinite loop
    }

    // Final joint matrices = globalTransform * inverseBindMatrix.
    for (int i = 0; i < jointCount; ++i) {
        anim.jointMatrices[i] =
            globalTransform[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

void AnimatorSetClip(Animator& anim, const Skeleton& skeleton, int clipIndex) {
    anim.clipIndex = clipIndex;
    anim.time = 0.0f;

    int jointCount = static_cast<int>(skeleton.joints.size());
    anim.jointMatrices.resize(jointCount, glm::mat4(1.0f));
}
