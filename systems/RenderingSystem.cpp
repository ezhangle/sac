#include "RenderingSystem.h"
#include "RenderingSystem_Private.h"
#include "base/EntityManager.h"
#include "TransformationSystem.h"
#include "CameraSystem.h"
#include <cmath>
#include <sstream>
#include "base/MathUtil.h"
#include "util/IntersectionUtil.h"
#include "opengl/OpenGLTextureCreator.h"
#if defined(ANDROID) || defined(EMSCRIPTEN)
#else
#include <sys/inotify.h>
#include <GL/glew.h>
#include <unistd.h>
#endif

#ifdef DEBUG
#include "base/Assert.h"
#endif
#ifdef INGAME_EDITORS
#include <AntTweakBar.h>
#endif

INSTANCE_IMPL(RenderingSystem);

RenderingSystem::RenderingSystem() : ComponentSystemImpl<RenderingComponent>("Rendering"), assetAPI(0), initDone(false) {
    nextValidFBRef = 1;
    currentWriteQueue = 0;
    frameQueueWritable = true;
    newFrameReady = false;
#ifndef EMSCRIPTEN
    mutexes = new std::mutex[3];
    cond = new std::condition_variable[2];
#endif

    RenderingComponent tc;
    componentSerializer.add(new Property<TextureRef>(OFFSET(texture, tc)));
    componentSerializer.add(new Property<EffectRef>(OFFSET(effectRef, tc)));
    componentSerializer.add(new Property<Color>(OFFSET(color, tc)));
    componentSerializer.add(new Property<bool>(OFFSET(hide, tc)));
    componentSerializer.add(new Property<bool>(OFFSET(mirrorH, tc)));
    componentSerializer.add(new Property<bool>(OFFSET(zPrePass, tc)));
    componentSerializer.add(new Property<bool>(OFFSET(fastCulling, tc)));
    componentSerializer.add(new Property<int>(OFFSET(opaqueType, tc)));
    componentSerializer.add(new Property<int>(OFFSET(cameraBitMask, tc)));

    InternalTexture::Invalid.color = InternalTexture::Invalid.alpha = 0;
    initDone = true;

    renderQueue = new RenderQueue[2];
}

RenderingSystem::~RenderingSystem() {
#ifndef EMSCRIPTEN
    delete[] mutexes;
    delete[] cond;
#endif
    initDone = false;
    delete[] renderQueue;
}

void RenderingSystem::setWindowSize(int width, int height, float sW, float sH) {
	windowW = width;
	windowH = height;
	screenW = sW;
	screenH = sH;
	GL_OPERATION(glViewport(0, 0, windowW, windowH))
    #ifdef INGAME_EDITORS
    TwWindowSize(width, height);
    #endif
}

void RenderingSystem::init() {
    LOG_IF(FATAL, !assetAPI) << "AssetAPI must be set before init is called";
    OpenGLTextureCreator::detectSupportedTextureFormat();
    textureLibrary.init(assetAPI);
    effectLibrary.init(assetAPI);

	defaultShader = effectLibrary.load(DEFAULT_FRAGMENT);
	defaultShaderNoAlpha = effectLibrary.load("default_no_alpha.fs");
    defaultShaderEmpty = effectLibrary.load("empty.fs");

    effectLibrary.update();

	GL_OPERATION(glClearColor(148.0/255, 148.0/255, 148.0/255, 1.0)) // temp setting for RR

	// create 1px white texture
	uint8_t data[] = {255, 255, 255, 255};
	GL_OPERATION(glGenTextures(1, &whiteTexture))
	GL_OPERATION(glBindTexture(GL_TEXTURE_2D, whiteTexture))
	GL_OPERATION(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT))// CLAMP_TO_EDGE))
	GL_OPERATION(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT))// CLAMP_TO_EDGE))
	GL_OPERATION(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR))
	GL_OPERATION(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR))
	GL_OPERATION(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1,
                1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                data))

	// GL_OPERATION(glEnable(GL_BLEND))
	GL_OPERATION(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA))
	GL_OPERATION(glEnable(GL_DEPTH_TEST))
	GL_OPERATION(glDepthFunc(GL_GREATER))
#if defined(ANDROID) || defined(EMSCRIPTEN)
	GL_OPERATION(glClearDepthf(0.0))
 #else
    GL_OPERATION(glClearDepth(0.0))
 #endif
	// GL_OPERATION(glDepthRangef(0, 1))
	GL_OPERATION(glDepthMask(false))

#ifdef USE_VBO
	glGenBuffers(3, squareBuffers);
GLfloat sqArray[] = {
-0.5, -0.5, 0,0,0,
0.5, -0.5, 0,1,0,
-0.5, 0.5, 0,0,1,
0.5, 0.5, 0,1,1
};

GLfloat sqArrayRev[] = {
0.5, -0.5, 0,0,0,
0.5, 0.5, 0,1,0,
-0.5, -0.5, 0,0,1,
-0.5, 0.5, 0,1,1
};
unsigned short sqIndiceArray[] = {
	0,1,2,1,3,2
};
// Buffer d'informations de vertex
glBindBuffer(GL_ARRAY_BUFFER, squareBuffers[0]);
glBufferData(GL_ARRAY_BUFFER, sizeof(sqArray), sqArray, GL_STATIC_DRAW);

glBindBuffer(GL_ARRAY_BUFFER, squareBuffers[1]);
glBufferData(GL_ARRAY_BUFFER, sizeof(sqArrayRev), sqArrayRev, GL_STATIC_DRAW);

// Buffer d'indices
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, squareBuffers[2]);
glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sqIndiceArray), sqIndiceArray, GL_STATIC_DRAW);

#endif
}
static bool sortFrontToBack(const RenderingSystem::RenderCommand& r1, const RenderingSystem::RenderCommand& r2) {
	if (r1.z == r2.z) {
		if (r1.effectRef == r2.effectRef)
			return r1.texture < r2.texture;
		else
			return r1.effectRef < r2.effectRef;
	} else
		return r1.z > r2.z;
}

static bool sortBackToFront(const RenderingSystem::RenderCommand& r1, const RenderingSystem::RenderCommand& r2) {
    #define EPSILON 0.0001
	if (MathUtil::Abs(r1.z - r2.z) <= EPSILON) {
		if (r1.effectRef == r2.effectRef) {
			if (r1.texture == r2.texture) {
                if (r1.flags != r2.flags)
                    return r1.flags > r2.flags;
                else
	                return r1.color < r2.color;
	        } else {
	            return r1.texture < r2.texture;
	        }
		} else {
			return r1.effectRef < r2.effectRef;
		}
	} else {
		return r1.z < r2.z;
    }
}

static inline void modifyQ(RenderingSystem::RenderCommand& r, const Vector2& offsetPos, const Vector2& size) {
    const Vector2 offset =  offsetPos * r.halfSize * 2 + size * r.halfSize * 2 * 0.5;
    r.position = r.position  + Vector2((r.mirrorH ? -1 : 1), 1) * Vector2::Rotate(- r.halfSize + offset, r.rotation);
    r.halfSize = size * r.halfSize;
}

static void modifyR(RenderingSystem::RenderCommand& r, const Vector2& offsetPos, const Vector2& size) {
    const Vector2 offset =  offsetPos * r.halfSize * 2 + size * r.halfSize * 2 * 0.5;
    r.position = r.position  + Vector2((r.mirrorH ? -1 : 1), 1) * Vector2::Rotate(- r.halfSize + offset, r.rotation);
    r.halfSize = size * r.halfSize;
    r.uv[0] = offsetPos;
    r.uv[1] = size;
}

static bool cull(const TransformationComponent* camera, RenderingSystem::RenderCommand& c) {
    if (c.rotation == 0 && c.halfSize.X > 0) {
        const float camLeft = camera->worldPosition.X - camera->size.X * .5f;
        const float camRight = camera->worldPosition.X + camera->size.X * .5f;

        const float invWidth = 1.0f / (2 * c.halfSize.X);
        // left culling !
        {
            float cullLeftX = camLeft - (c.position.X - c.halfSize.X);
            if (cullLeftX > 0) {
                if (cullLeftX >= 2 * c.halfSize.X)
                    return false;
                const float prop = cullLeftX * invWidth; // € [0, 1]
                if (!c.mirrorH) {
                    c.uv[0].X += prop * c.uv[1].X;
                }
                c.uv[1].X *= (1 - prop);
                c.halfSize.X *= (1 - prop);
                c.position.X += 0.5 * cullLeftX;
                return true;
            }
        }
        // right culling !
        {
            float cullRightX = (c.position.X + c.halfSize.X) - camRight;
            if (cullRightX > 0) {
                if (cullRightX >= 2 * c.halfSize.X)
                    return false;
                const float prop = cullRightX * invWidth; // € [0, 1]
                if (c.mirrorH) {
                    c.uv[0].X += prop * c.uv[1].X;
                }
                c.uv[1].X *= (1 - prop);
                c.halfSize.X *= (1 - prop);
                c.position.X -= 0.5 * cullRightX;
                return true;
            }
        }
    }
    return true;
}

void RenderingSystem::DoUpdate(float) {
    static unsigned int cccc = 0;
    RenderQueue& outQueue = renderQueue[currentWriteQueue];

    LOG_IF(WARNING, outQueue.count != 0) << "Non empty queue : " << outQueue.count << " (queue=" << currentWriteQueue << ')';

    // retrieve all cameras
    std::vector<Entity> cameras = theCameraSystem.RetrieveAllEntityWithComponent();
    // remove non active ones
    std::remove_if(cameras.begin(), cameras.end(), CameraSystem::isDisabled);
    // sort along z
    std::sort(cameras.begin(), cameras.end(), CameraSystem::sort);

    outQueue.count = 0;
    for (unsigned idx = 0; idx<cameras.size(); idx++) {
        const Entity camera = cameras[idx];
        const CameraComponent* camComp = CAMERA(camera);
        const TransformationComponent* camTrans = TRANSFORM(camera);

    	std::vector<RenderCommand> opaqueCommands, semiOpaqueCommands;

    	/* render */
        FOR_EACH_ENTITY_COMPONENT(Rendering, a, rc)
            bool ccc = rc->cameraBitMask & (0x1 << camComp->id);
    		if (rc->hide || rc->color.a <= 0 || !ccc ) {
    			continue;
    		}

    		const TransformationComponent* tc = TRANSFORM(a);
            if (!IntersectionUtil::rectangleRectangle(camTrans, tc)) {
                continue;
            }

            #ifdef DEBUG
            LOG_IF(WARNING, tc->worldZ <= 0 || tc->worldZ > 1) << "Entity '" << theEntityManager.entityName(a) << "' has invalid z value: " << tc->worldZ << ". Will not be drawn";
            #endif

    		RenderCommand c;
    		c.z = tc->worldZ;
    		c.texture = rc->texture;
    		c.effectRef = rc->effectRef;
    		c.halfSize = tc->size * 0.5f;
    		c.color = rc->color;
    		c.position = tc->worldPosition;
    		c.rotation = tc->worldRotation;
    		c.uv[0] = Vector2::Zero;
    		c.uv[1] = Vector2(1, 1);
            c.mirrorH = rc->mirrorH;
            c.fbo = rc->fbo;
            if (rc->zPrePass) {
                c.flags = (EnableZWriteBit | DisableBlendingBit | DisableColorWriteBit);
            } else if (rc->opaqueType == RenderingComponent::FULL_OPAQUE) {
                c.flags = (EnableZWriteBit | DisableBlendingBit | EnableColorWriteBit);
            } else {
                c.flags = (DisableZWriteBit | EnableBlendingBit | EnableColorWriteBit);
            }

            if (c.texture != InvalidTextureRef && !c.fbo) {
                const TextureInfo* info = textureLibrary.get(c.texture, false);
                if (info) {
                    int atlasIdx = info->atlasIndex;
                    // If atlas texture is not loaded yet, load it
                    if (atlasIdx >= 0 && atlas[atlasIdx].ref == InvalidTextureRef) {
                        LOG(INFO) << "Requested effective load of atlas '" << atlas[atlasIdx].name << "'";
                        atlas[atlasIdx].ref = textureLibrary.load(atlas[atlasIdx].name);
                    }

                    // Only display the required area of the texture
                    modifyQ(c, info->reduxStart, info->reduxSize);
            
                   #ifndef USE_VBO
                    if (rc->opaqueType != RenderingComponent::FULL_OPAQUE &&
                        c.color.a >= 1 &&
                        info->opaqueSize != Vector2::Zero &&
                        !rc->zPrePass) {
                        // add a smaller full-opaque block at the center
                        RenderCommand cCenter(c);
                        cCenter.flags = (EnableZWriteBit | DisableBlendingBit | EnableColorWriteBit);
                        modifyR(cCenter, info->opaqueStart, info->opaqueSize);

                        if (cull(camTrans, cCenter))
                            opaqueCommands.push_back(cCenter);

                        const float leftBorder = info->opaqueStart.X, rightBorder = info->opaqueStart.X + info->opaqueSize.X;
                        const float bottomBorder = info->opaqueStart.Y + info->opaqueSize.Y;
                        if (leftBorder > 0) {
                            RenderCommand cLeft(c);
                            modifyR(cLeft, Vector2::Zero, Vector2(leftBorder, 1));
                            if (cull(camTrans, cLeft))
                                semiOpaqueCommands.push_back(cLeft);
                        }

                        if (rightBorder < 1) {
                            RenderCommand cRight(c);
                            modifyR(cRight, Vector2(rightBorder, 0), Vector2(1 - rightBorder, 1));
                            if (cull(camTrans, cRight))
                                semiOpaqueCommands.push_back(cRight);
                        }

                        RenderCommand cTop(c);
                        modifyR(cTop, Vector2(leftBorder, 0), Vector2(rightBorder - leftBorder, info->opaqueStart.Y));
                        if (cull(camTrans, cTop))
                            semiOpaqueCommands.push_back(cTop);

                        RenderCommand cBottom(c);
                        modifyR(cBottom, Vector2(leftBorder, bottomBorder), Vector2(rightBorder - leftBorder, 1 - bottomBorder));
                        if (cull(camTrans, cBottom))
                            semiOpaqueCommands.push_back(cBottom);
                        continue;
                    }
                    #endif
                }
            }

             #ifndef USE_VBO
             if (!rc->fastCulling) {
                if (!cull(camTrans, c)) {
                    continue;
                }
             }
             #endif

            switch (rc->opaqueType) {
             	case RenderingComponent::NON_OPAQUE:
    	         	semiOpaqueCommands.push_back(c);
    	         	break;
    	         case RenderingComponent::FULL_OPAQUE:
    	         	opaqueCommands.push_back(c);
    	         	break;
                 default:
                    LOG(WARNING) << "Entity will not be drawn";
                    break;
            }
        }

        outQueue.commands.resize(
            outQueue.count + opaqueCommands.size() + semiOpaqueCommands.size() + 1);
    	std::sort(opaqueCommands.begin(), opaqueCommands.end(), sortFrontToBack);
    	std::sort(semiOpaqueCommands.begin(), semiOpaqueCommands.end(), sortBackToFront);

        RenderCommand dummy;
        dummy.texture = BeginFrameMarker;
        packCameraAttributes(camTrans, camComp, dummy);
        outQueue.commands[outQueue.count] = dummy;
        outQueue.count++;
        std::copy(opaqueCommands.begin(), opaqueCommands.end(), outQueue.commands.begin() + outQueue.count);//&outQueue.commands[outQueue.count]);
        outQueue.count += opaqueCommands.size();
        // semiOpaqueCommands.front().flags = (DisableZWriteBit | EnableBlendingBit);
        std::copy(semiOpaqueCommands.begin(), semiOpaqueCommands.end(), outQueue.commands.begin() + outQueue.count); //&outQueue.commands[outQueue.count]);
        outQueue.count += semiOpaqueCommands.size();
    }

    outQueue.commands.reserve(outQueue.count + 1);
    RenderCommand dummy;
    dummy.texture = EndFrameMarker;
    dummy.rotateUV = cccc;
    outQueue.commands[outQueue.count++] = dummy;
    outQueue.count++;
    std::stringstream framename;
    framename << "create-frame-" << cccc;
    PROFILE("Render", framename.str(), InstantEvent);
    cccc++;
    //LOGW("[%d] Added: %d + %d + 2 elt (%d frames) -> %d (%u)", currentWriteQueue, opaqueCommands.size(), semiOpaqueCommands.size(), outQueue.frameToRender, outQueue.commands.size(), dummy.rotateUV);
    //LOGW("Wrote frame %d commands to queue %d", outQueue.count, currentWriteQueue);

#ifndef EMSCRIPTEN
    // Lock to not change queue while ther thread is reading it
    mutexes[L_RENDER].lock();
    // Lock for notifying queue change
    mutexes[L_QUEUE].lock();
#endif
    currentWriteQueue = (currentWriteQueue + 1) % 2;
    newFrameReady = true;
#ifndef EMSCRIPTEN
    cond[C_FRAME_READY].notify_all();
    mutexes[L_QUEUE].unlock();
    mutexes[L_RENDER].unlock();
#endif
}

bool RenderingSystem::isEntityVisible(Entity e, int cameraIndex) const {
    return isVisible(TRANSFORM(e), cameraIndex);
}

bool RenderingSystem::isVisible(const TransformationComponent* tc, int cameraIndex) const {
    return true;
    #if 0
    if (cameraIndex < 0) {
        for (unsigned camIdx = 0; camIdx < cameras.size(); camIdx++) {
            if (isVisible(tc, camIdx)) {
                return true;
            }
        }
        return false;
    }
    const Camera& camera = cameras[cameraIndex];
	const Vector2 pos(tc->worldPosition - camera.worldPosition);
    const Vector2 camHalfSize(camera.worldSize * .5);

    const float biggestHalfEdge = MathUtil::Max(tc->size.X * 0.5, tc->size.Y * 0.5);
	if ((pos.X + biggestHalfEdge) < -camHalfSize.X) return false;
	if ((pos.X - biggestHalfEdge) > camHalfSize.X) return false;
	if ((pos.Y + biggestHalfEdge) < -camHalfSize.Y) return false;
	if ((pos.Y - biggestHalfEdge) > camHalfSize.Y) return false;
	return true;
    #endif
}

int RenderingSystem::saveInternalState(uint8_t** out) {
	int size = 0;
    LOG(WARNING) << "TODO";
    #if 0
    for (std::map<std::string, TextureRef>::iterator it=assetTextures.begin(); it!=assetTextures.end(); ++it) {
        size += (*it).first.length() + 1;
        size += sizeof(TextureRef);
        size += sizeof(TextureInfo);
    }

	*out = new uint8_t[size];
	uint8_t* ptr = *out;

	for (std::map<std::string, TextureRef>::iterator it=assetTextures.begin(); it!=assetTextures.end(); ++it) {
		ptr = (uint8_t*) mempcpy(ptr, (*it).first.c_str(), (*it).first.length() + 1);
		ptr = (uint8_t*) mempcpy(ptr, &(*it).second, sizeof(TextureRef));
		ptr = (uint8_t*) mempcpy(ptr, &(textures[it->second]), sizeof(TextureInfo));
	}
    #endif
	return size;
}

void RenderingSystem::restoreInternalState(const uint8_t* in, int size) {
    LOG(WARNING) << "TODO";
    #if 0
	assetTextures.clear();
	textures.clear();
	nextValidRef = 1;
	nextEffectRef = 1;
	int idx = 0;

	while (idx < size) {
		char name[128];
		int i=0;
		do {
			name[i] = in[idx++];
		} while (name[i++] != '\0');
		TextureRef ref;
		memcpy(&ref, &in[idx], sizeof(TextureRef));
		idx += sizeof(TextureRef);
		TextureInfo info;
		memcpy(&info, &in[idx], sizeof(TextureInfo));
		idx += sizeof(TextureInfo);

		assetTextures[name] = ref;
		if (info.atlasIndex >= 0) {
			info.glref = atlas[info.atlasIndex].glref;
			textures[ref] = info;
		}
		nextValidRef = MathUtil::Max(nextValidRef, ref + 1);
	}
	for (std::map<std::string, EffectRef>::iterator it=nameToEffectRefs.begin();
		it != nameToEffectRefs.end();
		++it) {
		nextEffectRef = MathUtil::Max(nextEffectRef, it->second + 1);
	}
    #endif
}

void RenderingSystem::setFrameQueueWritable(bool b) {
    if (frameQueueWritable == b || !initDone)
        return;
    LOG(INFO) << "Writable: " << b;
#ifndef EMSCRIPTEN
    mutexes[L_QUEUE].lock();
#endif
    frameQueueWritable = b;
#ifndef EMSCRIPTEN
    cond[C_FRAME_READY].notify_all();
    mutexes[L_QUEUE].unlock();

    mutexes[L_RENDER].lock();
    cond[C_RENDER_DONE].notify_all();
    mutexes[L_RENDER].unlock();
#endif
}

FramebufferRef RenderingSystem::createFramebuffer(const std::string& name, int width, int height) {
    Framebuffer fb;
    fb.width = width;
    fb.height = height;

    // create a texture object
    glGenTextures(1, &fb.texture);
    glBindTexture(GL_TEXTURE_2D, fb.texture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // create a renderbuffer object to store depth info
    glGenRenderbuffers(1, &fb.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // create a framebuffer object
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    // attach the texture to FBO color attachment point
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.texture, 0);
    // attach the renderbuffer to depth attachment point
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);

    // check FBO status
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE)
        LOG(FATAL) << "FBO not complete: " << status;

    // switch back to window-system-provided framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    FramebufferRef result = nextValidFBRef++;
    nameToFramebuffer[name] = result;
    ref2Framebuffers[result] = fb;
    return result;
}

FramebufferRef RenderingSystem::getFramebuffer(const std::string& fbName) const {
    std::map<std::string, FramebufferRef>::const_iterator it = nameToFramebuffer.find(fbName);
    if (it == nameToFramebuffer.end())
        LOG(FATAL) << "Framebuffer '" << fbName << "' does not exist";
    return it->second;
}

void packCameraAttributes(
    const TransformationComponent* cameraTrans,
    const CameraComponent* cameraComp,
    RenderingSystem::RenderCommand& out) {
    out.uv[0] = cameraTrans->worldPosition;
    out.uv[1] = cameraTrans->size;
    out.z = cameraTrans->worldRotation;

    out.rotateUV = cameraComp->fb;
    out.color = cameraComp->clearColor;
}

void unpackCameraAttributes(
    const RenderingSystem::RenderCommand& in,
    TransformationComponent* camera,
    CameraComponent* ccc) {
    camera->worldPosition = in.uv[0];
    camera->size = in.uv[1];
    camera->worldRotation = in.z;

    ccc->fb = in.rotateUV;
    ccc->clearColor = in.color;
}

#ifdef INGAME_EDITORS
void RenderingSystem::addEntityPropertiesToBar(Entity entity, TwBar* bar) {
    RenderingComponent* tc = Get(entity, false);
    if (!tc) return;
    TwAddVarRW(bar, "color", TW_TYPE_COLOR4F, &tc->color, "group=Rendering");
    TwAddVarRW(bar, "hide", TW_TYPE_BOOLCPP, &tc->hide, "group=Rendering");
    TwEnumVal opa[] = { {RenderingComponent::NON_OPAQUE, "NonOpaque"}, {RenderingComponent::FULL_OPAQUE, "Opaque"} };
    TwType op = TwDefineEnum("OpaqueType", opa, 2);
    TwAddVarRW(bar, "opaque", op, &tc->opaqueType, "group=Rendering");
    TwAddVarRW(bar, "effect", TW_TYPE_INT32, &tc->effectRef, "group=Rendering");
    TwAddVarRW(bar, "texture", TW_TYPE_INT32, &tc->texture, "group=Rendering");
}
#endif
