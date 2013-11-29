/*
    This file is part of Soupe Au Caillou.

    @author Soupe au Caillou - Jordane Pelloux-Prayer
    @author Soupe au Caillou - Gautier Pelloux-Prayer
    @author Soupe au Caillou - Pierre-Eric Pelloux-Prayer

    Soupe Au Caillou is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    Soupe Au Caillou is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Soupe Au Caillou.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "AnimationSystem.h"
#include "TransformationSystem.h"
#include "RenderingSystem.h"
#include "opengl/AnimDescriptor.h"

static void applyFrameToEntity(Entity e, const AnimationComponent* animComp, const AnimDescriptor::AnimFrame& frame) {
    LOGV(1, "animation: " << theEntityManager.entityName(e) << ": new frame = '" << frame.texture << "'");

    // hum..RENDERING(e)->show = (frame.texture != InvalidTextureRef);
    RENDERING(e)->texture = frame.texture;
    LOGW_IF(animComp->subPart.size() != frame.transforms.size(), "Animation entity subpart count " << animComp->subPart.size() << " is different from frame transform count " << frame.transforms.size());
    for (unsigned i=0; i<frame.transforms.size() && i<animComp->subPart.size(); i++) {
        TransformationComponent* tc = TRANSFORM(animComp->subPart[i]);
        const AnimDescriptor::AnimFrame::Transform& trans = frame.transforms[i];
        tc->position = trans.position;
        tc->size = trans.size;
        tc->rotation = trans.rotation;
    }
}

INSTANCE_IMPL(AnimationSystem);

AnimationSystem::AnimationSystem() : ComponentSystemImpl<AnimationComponent>("Animation") {
    AnimationComponent tc;
    componentSerializer.add(new StringProperty("name", OFFSET(name, tc)));
    componentSerializer.add(new Property<float>("playback_speed", OFFSET(playbackSpeed, tc), 0.001f));
    componentSerializer.add(new Property<float>("accum", OFFSET(accum, tc), 0.001f));
    componentSerializer.add(new Property<float>("wait_accum", OFFSET(waitAccum, tc), 0.001f));
    componentSerializer.add(new Property<int>("loop_count", OFFSET(loopCount, tc)));
    componentSerializer.add(new Property<int>("frame_index", OFFSET(frameIndex, tc)));
}

AnimationSystem::~AnimationSystem() {
    for(AnimIt it=animations.begin(); it!=animations.end(); ++it) {
        delete it->second;
    }
}

void AnimationSystem::DoUpdate(float dt) {
    FOR_EACH_ENTITY_COMPONENT(Animation, a, bc)
        if (bc->name.empty())
            continue;
        AnimIt jt = animations.find(bc->name);
        if (jt == animations.end()) {
            LOGW("Animation '" << bc->name << "' not found. " << animations.size() << " defined animation(s):");
            for (auto an: animations) {
                LOGW("   '" << an.first << "' - " << an.second->frames.size() << " frames");
            }
            LOGF_IF(animations.empty(), "Weird, no animations loaded");
            continue;
        }
        AnimDescriptor* anim = jt->second;

        if (bc->previousName != bc->name) {
            bc->frameIndex = 0;
            applyFrameToEntity(a, bc, anim->frames[bc->frameIndex]);
            bc->accum = 0;
            bc->previousName = bc->name;
            bc->loopCount = anim->loopCount.random();
        } else if (bc->playbackSpeed > 0) {
            if (bc->waitAccum > 0) {
                bc->waitAccum -= dt;
                if (bc->waitAccum <= 0) {
                    bc->waitAccum = 0;
                    RENDERING(a)->show = true;
                }
            } else {
                bc->accum += dt * anim->playbackSpeed * bc->playbackSpeed;
            }

            while(bc->accum >= 1) {
                bool lastImage = (bc->frameIndex == (int)anim->frames.size() - 1);
                if (lastImage) {
                    if (bc->loopCount != 0) {
                        bc->frameIndex = 0;
                        bc->loopCount--;

                        /*if (bc->loopCount == 0) {
                            bc->name = bc->previousName = "";
                            break;
                        }*/
                    } else if (!anim->nextAnim.empty()) {
                        if ((bc->waitAccum = anim->nextAnimWait.random()) > 0)
                            RENDERING(a)->show = false;
                        bc->name = anim->nextAnim;
                        // hum, maybe bc->waitAccum should be -= accum leftover..
                        bc->accum = 0;
                        break;
                    }
                } else {
                    bc->frameIndex++;
                }
                applyFrameToEntity(a, bc, anim->frames[bc->frameIndex]);
                // RENDERING(a)->texture = anim->frames[bc->frameIndex].texture;
                bc->accum -= 1;
            }
        }
    END_FOR_EACH()
}

void AnimationSystem::loadAnim(AssetAPI* assetAPI, const std::string& name, const std::string& filename, std::string* variables, int varcount) {
    const std::string fileN("anim/" + filename + ".anim");
    FileBuffer file = assetAPI->loadAsset(fileN);
    if (file.size) {
        AnimDescriptor* desc = new AnimDescriptor;
        if (desc->load(fileN, file, variables, varcount)) {
            animations.insert(std::make_pair(name, desc));
        } else {
            LOGE("Invalid animation file: " << filename << ".anim");
            delete desc;
        }
    } else {
        LOGE("Empty animation file: " << filename << ".anim");
    }
    delete[] file.data;
}
