// Skeletal animation data types.
//
// A Skeleton is a hierarchy of Joints with inverse bind matrices.
// An AnimationClip stores sampled keyframes (position, rotation, scale)
// per joint over time.

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

// GL 4.1 uniform limit: 4096 components / 16 per mat4 = 256 max.
// 128 leaves headroom for other uniforms.
constexpr int kMaxJoints = 128;

struct Joint {
    std::string name;
    int         parent = -1;              // -1 = root joint
    glm::mat4   inverseBindMatrix{1.0f};  // mesh space -> bone space

    // Bind-pose local transform — used as default when a joint has no
    // animation channel. Without these, un-animated joints collapse to
    // identity which distorts the skeleton.
    glm::vec3 bindPosition{0.0f};
    glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bindScale{1.0f};
};

struct Skeleton {
    std::vector<Joint> joints;
};

// Keyframe types — sampled at discrete times.
struct PosKey   { float time; glm::vec3 value; };
struct RotKey   { float time; glm::quat value; };
struct ScaleKey { float time; glm::vec3 value; };

// One channel drives one joint's TRS over time.
struct AnimChannel {
    int jointIndex = -1;
    std::vector<PosKey>   positions;
    std::vector<RotKey>   rotations;
    std::vector<ScaleKey> scales;
};

struct AnimationClip {
    std::string              name;
    float                    duration = 0.0f;
    std::vector<AnimChannel> channels;
};
