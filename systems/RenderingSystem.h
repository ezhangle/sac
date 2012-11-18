/*
	This file is part of Heriswap.

	@author Soupe au Caillou - Pierre-Eric Pelloux-Prayer
	@author Soupe au Caillou - Gautier Pelloux-Prayer

	Heriswap is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	Heriswap is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Heriswap.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#if defined(ANDROID) || defined(EMSCRIPTEN)
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#else
#include <GL/glew.h>
#endif
#include <map>
#include <set>
#include <queue>
#include <list>

#include "base/Vector2.h"
#include "base/MathUtil.h"
#include "base/Log.h"
#include "base/Color.h"
#include "../api/AssetAPI.h"

#include "System.h"
#include "TransformationSystem.h"

typedef int TextureRef;
#define InvalidTextureRef -1

#ifdef EMSCRIPTEN
#define USE_VBO
#endif

typedef int EffectRef;
#define DefaultEffectRef -1

struct RenderingComponent {
	RenderingComponent() : texture(InvalidTextureRef), effectRef(DefaultEffectRef), color(Color()), hide(true), mirrorH(false), opaqueType(NON_OPAQUE), cameraBitMask(~0U) {}

	TextureRef texture;
	EffectRef effectRef;
	Color color;
	bool hide, mirrorH;
	enum Opacity {
		NON_OPAQUE = 0,
		FULL_OPAQUE,
		OPAQUE_ABOVE,
		OPAQUE_UNDER,
		OPAQUE_CENTER,
	} ;
	Opacity opaqueType;
	float opaqueSeparation; // €[0, 1], meaning depends of opaqueType
    unsigned cameraBitMask;
};

#define theRenderingSystem RenderingSystem::GetInstance()
#define RENDERING(e) theRenderingSystem.Get(e)

UPDATABLE_SYSTEM(Rendering)

public:
void init();

void loadAtlas(const std::string& atlasName, bool forceImmediateTextureLoading = false);
void unloadAtlas(const std::string& atlasName);
void invalidateAtlasTextures();

int saveInternalState(uint8_t** out);
void restoreInternalState(const uint8_t* in, int size);

void setWindowSize(int w, int h, float sW, float sH);

TextureRef loadTextureFile(const std::string& assetName);
EffectRef loadEffectFile(const std::string& assetName);
void unloadTexture(TextureRef ref, bool allowUnloadAtlas = false);

public:
AssetAPI* assetAPI;

bool isEntityVisible(Entity e, int cameraIndex = -1);
bool isVisible(const TransformationComponent* tc, int cameraIndex = -1);

void reloadTextures();
void reloadEffects();

void render();
void waitDrawingComplete();

public:
int windowW, windowH;
float screenW, screenH;

struct Camera {
    Camera() {}
    Camera(const Vector2& pWorldPos, const Vector2& pWorldSize, const Vector2& pScreenPos, const Vector2& pScreenSize) :
        worldPosition(pWorldPos), worldSize(pWorldSize), screenPosition(pScreenPos), screenSize(pScreenSize), enable(true), mirrorY(false) {}
    Vector2 worldPosition, worldSize;
    Vector2 screenPosition, screenSize;
    bool enable, mirrorY;
};

/* textures cache */
TextureRef nextValidRef;
std::map<std::string, TextureRef> assetTextures;

struct InternalTexture {
	GLuint color;
	GLuint alpha;

	bool operator==(const InternalTexture& t) const {
		return color == t.color && alpha == t.alpha;
	}
	bool operator!=(const InternalTexture& t) const {
		return color != t.color || alpha != t.alpha;
	}
	bool operator<(const InternalTexture& t) const {
		return color < t.color;
	}

	static InternalTexture Invalid;
};

struct RenderCommand {
	float z;
	EffectRef effectRef;
	union {
		TextureRef texture;
		InternalTexture glref;
	};
	unsigned int rotateUV;
	Vector2 uv[2];
	Vector2 halfSize;
	Color color;
	Vector2 position;
	float rotation;
    bool mirrorH;
};

struct RenderQueue {
	RenderQueue() : count(0) {}
	uint16_t count;
	RenderCommand commands[512];
};

struct TextureInfo {
	InternalTexture glref;
	unsigned int rotateUV;
	Vector2 uv[2];
	int atlasIndex;
	Vector2 size;
    Vector2 opaqueStart, opaqueSize;

	TextureInfo (const InternalTexture& glref = InternalTexture::Invalid, int x = 0, int y = 0, int w = 0, int h = 0, bool rot = false, const Vector2& size = Vector2::Zero, const Vector2& opaqueStart = Vector2::Zero, const Vector2& opaqueSize=Vector2::Zero, int atlasIdx = -1);
};
struct Atlas {
	std::string name;
	InternalTexture glref;
};

std::map<TextureRef, TextureInfo> textures;
std::set<std::string> delayedLoads;
std::set<int> delayedAtlasIndexLoad;
std::set<InternalTexture> delayedDeletes;
std::vector<Atlas> atlas;
std::vector<Camera> cameras;

bool newFrameReady;
int currentWriteQueue;
RenderQueue renderQueue[2];
#ifdef USE_VBO
public:
GLuint squareBuffers[3];
private:
#endif

#ifndef EMSCRIPTEN
pthread_mutex_t mutexes[3];
pthread_cond_t cond[2];
#endif

struct Shader {
	GLuint program;
	GLuint uniformMatrix, uniformColorSampler, uniformAlphaSampler, uniformColor;
	#ifdef USE_VBO
	GLuint uniformUVScaleOffset, uniformRotation, uniformScale;
	#endif
};

Shader defaultShader, defaultShaderNoAlpha;
GLuint whiteTexture;

EffectRef nextEffectRef;
std::map<std::string, EffectRef> nameToEffectRefs;
std::map<EffectRef, Shader> ref2Effects;

/* open gl es1 var */

#ifndef ANDROID
int inotifyFd;
struct NotifyInfo {
    int wd;
    std::string asset;
};
std::vector<NotifyInfo> notifyList;
#endif

private:
static void loadOrthographicMatrix(float left, float right, float bottom, float top, float near, float far, float* mat);
GLuint compileShader(const std::string& assetName, GLuint type);
void loadTexture(const std::string& assetName, Vector2& realSize, Vector2& pow2Size, InternalTexture& out);
void drawRenderCommands(RenderQueue& commands);
void processDelayedTextureJobs();
GLuint createGLTexture(const std::string& basename, bool colorOrAlpha, Vector2& realSize, Vector2& pow2Size);
public:
static void check_GL_errors(const char* context);
Shader buildShader(const std::string& vs, const std::string& fs);
EffectRef changeShaderProgram(EffectRef ref, bool firstCall, const Color& color, const Camera& camera);
const Shader& effectRefToShader(EffectRef ref, bool firstCall);
const Vector2& getTextureSize(const std::string& textureName) const;
void removeExcessiveFrames(int& readQueue, int& writeQueue);
bool pvrSupported;
};
