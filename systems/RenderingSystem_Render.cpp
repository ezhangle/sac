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



#include "RenderingSystem.h"
#include "opengl/OpenglHelper.h"
#include "RenderingSystem_Private.h"
#include "CameraSystem.h"
#include "TransformationSystem.h"
#include "base/Profiler.h"

#include <sstream>
#if SAC_INGAME_EDITORS
#include "util/LevelEditor.h"
#include "imgui.h"
#endif
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#if SAC_DEBUG
#include "RenderingSystem_Debug.h"
#endif

#if SAC_INGAME_EDITORS
GLuint RenderingSystem::fontTex;
GLuint RenderingSystem::leProgram, RenderingSystem::leProgramuniformColorSampler, RenderingSystem::leProgramuniformMatrix;
#endif


static void computeVerticesScreenPos(const std::vector<glm::vec2>& points, const glm::vec2& position, const glm::vec2& hSize, float rotation, float z, VertexData* out);

GLuint activeProgramColorU;

RenderingSystem::ColorAlphaTextures RenderingSystem::chooseTextures(const InternalTexture& tex, const FramebufferRef& fbo, bool useFbo) {
    if (useFbo) {
        RenderingSystem::Framebuffer b = ref2Framebuffers[fbo];
        return std::make_pair(b.texture, b.texture);
    } else {
        return std::make_pair(tex.color, tex.alpha);
    }
}

static Buffers::Enum previousActiveVertexBuffer = Buffers::Count; /* Invalid value */

static void changeVertexBuffer(GLuint newBuffer, Buffers::Enum val) {
    // if (previousActiveVertexBuffer ==
    GL_OPERATION(glBindBuffer(GL_ARRAY_BUFFER, newBuffer))

    GL_OPERATION(glEnableVertexAttribArray(EffectLibrary::ATTRIB_VERTEX))

    GL_OPERATION(glVertexAttribPointer(EffectLibrary::ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData), 0))
    GL_OPERATION(glEnableVertexAttribArray(EffectLibrary::ATTRIB_UV))
    GL_OPERATION(glVertexAttribPointer(EffectLibrary::ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)sizeof(glm::vec3)))

    previousActiveVertexBuffer = val;
}

static int drawBatchES2(
    const VertexData* vertices
    , const unsigned short* indices
    , int batchVertexCount
    , unsigned indiceCount
    , Buffers::Enum activeVertexBuffer
    ) {

    if (indiceCount > 0) {

        // bind proper vertex buffer (if needed)
        if (previousActiveVertexBuffer != activeVertexBuffer) {
            changeVertexBuffer(theRenderingSystem.glBuffers[activeVertexBuffer], activeVertexBuffer);
        }

        if (activeVertexBuffer == Buffers::Dynamic) {
            // orphan previous storage
            GL_OPERATION(glBufferData(GL_ARRAY_BUFFER, MAX_VERTEX_COUNT * sizeof(VertexData), 0, GL_STREAM_DRAW))
            // update buffer
            GL_OPERATION(glBufferSubData(GL_ARRAY_BUFFER, 0,
                batchVertexCount * sizeof(VertexData), vertices))
        }

        // orphan
        GL_OPERATION(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            sizeof(unsigned short) * MAX_INDICE_COUNT, 0, GL_STREAM_DRAW))
        // update
        GL_OPERATION(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
            (indiceCount - 2) /*batchTriangleCount * 3*/ * sizeof(unsigned short), &indices[1]))

        GL_OPERATION(glDrawElements(GL_TRIANGLE_STRIP, indiceCount - 2/*batchTriangleCount * 3*/, GL_UNSIGNED_SHORT, 0))
    }

    #if SAC_OLD_HARDWARE
        //seems to solve artifacts on old graphic cards (nvidia 8600GT at least)
        glFinish();
    #endif
    return 0;
}

static inline void computeUV(RenderingSystem::RenderCommand& rc, const TextureInfo& info) {
    // Those 2 are used by RenderingSystem to display part of the texture, with different flags.
    // For instance: display a partial-but-opaque-version before the original alpha-blended one.
    // So, their default value are: offset=0,0 and size=1,1
    glm::vec2 uvModifierOffset(rc.uv[0]);
    glm::vec2 uvModifierSize(rc.uv[1]);
    const glm::vec2 uvSize (info.uv[1] - info.uv[0]);

     // If image is rotated in atlas, we need to adjust UVs
    if (info.rotateUV) {
        // #1: swap x/y start coordinates (ie: top-left point of the image)
        std::swap(uvModifierOffset.x, uvModifierOffset.y);
        // #2: swap x/y end coords (ie: bottom-right corner of the image)
        std::swap(uvModifierSize.x, uvModifierSize.y);
        // #3:
        uvModifierOffset.y = 1 - (uvModifierSize.y + uvModifierOffset.y);
        //uvModifierOffset.x = 1 - (uvModifierSize.x + uvModifierOffset.x);

    }

    // Compute UV to send to GPU
    {
        rc.uv[0] = info.uv[0] + glm::vec2(uvModifierOffset.x * uvSize.x, uvModifierOffset.y * uvSize.y);
        rc.uv[1] = rc.uv[0] + glm::vec2(uvModifierSize.x * uvSize.x, uvModifierSize.y * uvSize.y);
    }
    // Miror UV when doing horizontal miroring
    if (rc.rflags & RenderingFlags::MirrorHorizontal) {
        if (info.rotateUV)
            std::swap(rc.uv[0].y, rc.uv[1].y);
        else
            std::swap(rc.uv[0].x, rc.uv[1].x);
    }
    rc.rotateUV = info.rotateUV;
}

static inline void addRenderCommandToBatch(const RenderingSystem::RenderCommand& rc,
    const Polygon& polygon,
    VertexData* outVertices,
    unsigned short* outIndices,
    unsigned* verticesCount,
#if SAC_DEBUG
    unsigned* triangleCount,
#else
    unsigned*,
#endif
    unsigned* indiceCount) {

    uint16_t offset = *verticesCount;

    // vertices
    const std::vector<glm::vec2>& vert = polygon.vertices;

    // perform world -> screen position transformation (if needed)
    bool vertexBufferUpdateNeeded = (
        (!(rc.rflags & RenderingFlags::Constant))
            ||
        (rc.rflags & RenderingFlags::ConstantNeedsUpdate)
            );

    if (vertexBufferUpdateNeeded) {
        computeVerticesScreenPos(vert, rc.position, rc.halfSize, rc.rotation, -rc.z, outVertices);

    }
    if (rc.rflags & RenderingFlags::Constant)
        offset = rc.indiceOffset;

    // copy indices
    *outIndices++ = offset + polygon.indices[0];
    for(const auto& i: polygon.indices) {
        *outIndices++ = offset + i;
    }
    *outIndices++ = offset + polygon.indices.back();

    // copy uvs
    int mapping[][4] = {
        {0, 1, 3, 2},
        {1, 2, 0, 3}
    };

    if (vertexBufferUpdateNeeded) {
        outVertices[mapping[rc.rotateUV][0]].uv = glm::vec2(rc.uv[0].x, 1 - rc.uv[0].y);
        outVertices[mapping[rc.rotateUV][1]].uv = glm::vec2(rc.uv[1].x, 1 - rc.uv[0].y);
        outVertices[mapping[rc.rotateUV][2]].uv = glm::vec2(rc.uv[0].x, 1 - rc.uv[1].y);
        outVertices[mapping[rc.rotateUV][3]].uv = glm::vec2(rc.uv[1].x, 1 - rc.uv[1].y);
    }

    if (!(rc.rflags & RenderingFlags::Constant)) {
        *verticesCount += polygon.vertices.size();
    } else if (vertexBufferUpdateNeeded) {
        LOGI("Update constant buffer @" << rc.indiceOffset);
        // update constant buffer
        GL_OPERATION(glBindBuffer(GL_ARRAY_BUFFER, theRenderingSystem.glBuffers[Buffers::Static]))
        GL_OPERATION(glBufferSubData(GL_ARRAY_BUFFER,
            rc.indiceOffset * sizeof(VertexData),
            vert.size() * sizeof(VertexData),
            outVertices))

    }
    *indiceCount += 2 + polygon.indices.size();
#if SAC_DEBUG
    *triangleCount += polygon.indices.size() / 3;
#endif
}

Buffers::Enum RenderingSystem::changeShaderProgram(EffectRef ref, const Color& color, const glm::mat4& mvp) {
    const Shader& shader = *effectLibrary.get(ref, false);
    // change active shader
    GL_OPERATION(glUseProgram(shader.program))
    // upload transform matrix (perspective + view)
    GL_OPERATION( glUniformMatrix4fv(shader.uniformMatrix, 1, GL_FALSE, glm::value_ptr(mvp)))
    // upload texture uniforms
    GL_OPERATION(glUniform1i(shader.uniformColorSampler, 0))
    if (shader.uniformAlphaSampler != (unsigned int)(~0)) {
        GL_OPERATION(glUniform1i(shader.uniformAlphaSampler, 1))
    }
    // upload color uniform
    activeProgramColorU = shader.uniformColor;
    GL_OPERATION(glUniform4fv(activeProgramColorU, 1, color.rgba))

    /* Rebind vertex buffer if valid */
    Buffers::Enum b = previousActiveVertexBuffer;
    if (b == Buffers::Count) {
        b = Buffers::Static;
    }
    changeVertexBuffer(glBuffers[b], b);

    GL_OPERATION(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffers[Buffers::Indice]))

    return b;
}

void RenderingSystem::drawRenderCommands(RenderQueue& commands) {
    // Worst case scenario: 3 vertices per triangle (no shared vertices)
    unsigned indiceCount = 0;
    // Rendering state
    struct {
        TransformationComponent worldPos;
        CameraComponent cameraAttr;
    } camera;
    InternalTexture boundTexture = InternalTexture::Invalid;
    FramebufferRef fboRef = DefaultFrameBufferRef;
    EffectRef currentEffect = InvalidTextureRef;
    EffectRef activeDefaultEffect = InvalidTextureRef;
    Color currentColor(1,1,1,1);
    int currentFlags = glState.flags.current;
    bool useFbo = false;

    // Batch variable
    unsigned int batchVertexCount = 0;

    // matrices
    glm::mat4 camViewPerspMatrix;

    LOGV(3, "Begin frame rendering: " << commands.count);

    #if SAC_DEBUG
    check_GL_errors("Frame start");
    #endif

    #if SAC_INGAME_EDITORS && SAC_DESKTOP
    GL_OPERATION(glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL))
    if (wireframe) {
        GL_OPERATION(glLineWidth(2))
        GL_OPERATION(glEnable(GL_LINE_SMOOTH))
    } else {
        GL_OPERATION(glDisable(GL_LINE_SMOOTH))
    }
    #endif


    // Setup initial GL state
    GL_OPERATION(glActiveTexture(GL_TEXTURE1))
    GL_OPERATION(glBindTexture(GL_TEXTURE_2D, 0))
    GL_OPERATION(glActiveTexture(GL_TEXTURE0))

    #if SAC_DEBUG
    unsigned int batchTriangleCount = 0;
    batchSizes.clear();
    batchContent.clear();
    #endif

    Buffers::Enum activeVertexBuffer = Buffers::Count; /* invalid value */
    const TextureInfo* previousAtlasInfo = 0;
    TextureRef previousAtlasRef = -1;

    // The idea here is to browse through the list of _ordered_ list of
    // render command to execute. We try to group (batch) them in single
    // GL commands. When a GL state change is required (new color, new
    // texture, etc), we flush (execute) the active batch, and start
    // building a new one.
    const unsigned count = commands.count;
    for (unsigned i=0; i< count; i++) {
        RenderCommand& rc = commands.commands[i];

        // HANDLE BEGIN/END FRAME MARKERS (new frame OR new camera)
        if (rc.texture == BeginFrameMarker) {
            #if SAC_DEBUG
            batchSizes.push_back(std::make_pair(BatchFlushReason::NewCamera, batchTriangleCount));
            batchTriangleCount = 0;
            #endif
            indiceCount = batchVertexCount = drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);

            PROFILE("Render", "begin-render-frame", InstantEvent);

            unpackCameraAttributes(rc, &camera.worldPos, &camera.cameraAttr);
            LOGV(3, "   camera: pos=" << camera.worldPos.position.x << ',' << camera.worldPos.position.y
                << "size=" << camera.worldPos.size.x << ',' << camera.worldPos.size.y
                << " fb=" << camera.cameraAttr.fb);

            FramebufferRef fboRef = camera.cameraAttr.fb;
            if (fboRef == DefaultFrameBufferRef) {
                //glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glState.viewport.update(windowW, windowH);
            } else {
                const Framebuffer& fb = ref2Framebuffers[fboRef];
                glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
                glState.viewport.update(fb.width, fb.height);
            }

            // setup transformation matrix (based on camera attributes)
            camViewPerspMatrix =
                glm::ortho(-camera.worldPos.size.x * 0.5f, camera.worldPos.size.x * 0.5f,
                    -camera.worldPos.size.y * 0.5f, camera.worldPos.size.y * 0.5f,
                    0.0f, 1.0f) *
                glm::rotate( glm::mat4(1.0f),
                    -camera.worldPos.rotation, glm::vec3(0, 0, 1) ) *
                glm::translate( glm::mat4(1.0f),
                    glm::vec3(-camera.worldPos.position, 0.0f));

            glState.flags.update(OpaqueFlagSet);
            changeVertexBuffer(theRenderingSystem.glBuffers[Buffers::Dynamic], Buffers::Dynamic);
            activeVertexBuffer = Buffers::Dynamic;
            currentFlags = glState.flags.current;
            // GL_OPERATION(glDepthMask(true))
            /*GL_OPERATION(glDisable(GL_BLEND))
            GL_OPERATION(glColorMask(true, true, true, true))
            currentFlags = OpaqueFlagSet;*/
            if (camera.cameraAttr.clear) {
                glState.clear.update(camera.cameraAttr.clearColor);
                GL_OPERATION(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT))

            }
            continue;
        } else if (rc.texture == EndFrameMarker) {
            break;
        }

        // HANDLE RENDERING FLAGS (GL state switch)
        if (rc.flags != currentFlags) {
            #if SAC_DEBUG
            batchSizes.push_back(std::make_pair(BatchFlushInfo(BatchFlushReason::NewFlags, rc.flags), batchTriangleCount));
            batchTriangleCount = 0;
            #endif
            // flush batch before changing state
            indiceCount = batchVertexCount = drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);
            const bool useTexturing = (rc.texture != InvalidTextureRef);

            const int flagBitsChanged = glState.flags.update(rc.flags);

            // iff EnableBlendingBit changed
            if (flagBitsChanged & EnableBlendingBit ) {
                if (rc.flags & EnableBlendingBit) {
                    if (currentEffect == DefaultEffectRef) {
                        EffectRef newDefaultCandidate = chooseDefaultShader(true /* blending-on */, true /* color-on */, useTexturing);
                        if (newDefaultCandidate != activeDefaultEffect) {
                            activeDefaultEffect = newDefaultCandidate;
                            activeVertexBuffer = changeShaderProgram(activeDefaultEffect, currentColor, camViewPerspMatrix);
                        }
                    }
                }
            }

            // iff EnableColorWriteBit changed
            if (flagBitsChanged & EnableColorWriteBit ) {
                if (currentEffect == DefaultEffectRef) {
                    EffectRef newDefaultCandidate = chooseDefaultShader((rc.flags & EnableBlendingBit), (rc.flags & EnableColorWriteBit), useTexturing);
                    if (newDefaultCandidate != activeDefaultEffect) {
                        activeDefaultEffect = newDefaultCandidate;
                        activeVertexBuffer = changeShaderProgram(activeDefaultEffect, currentColor, camViewPerspMatrix);
                    }
                }
            }

            if (flagBitsChanged & EnableConstantBit) {
                if (rc.rflags & RenderingFlags::Constant)
                    activeVertexBuffer = Buffers::Static;
                else
                    activeVertexBuffer = Buffers::Dynamic;
            }
            currentFlags = glState.flags.current;
        }
        // EFFECT HAS CHANGED ?
        if (rc.effectRef != currentEffect) {
            #if SAC_DEBUG
            batchSizes.push_back(std::make_pair(BatchFlushReason::NewEffect, batchTriangleCount));
            batchTriangleCount = 0;
            #endif
            // flush before changing effect
            indiceCount = batchVertexCount = drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);
            const bool useTexturing = (rc.texture != InvalidTextureRef);

            currentEffect = rc.effectRef;
            if (currentEffect == DefaultEffectRef) {
                activeDefaultEffect = chooseDefaultShader((rc.flags & EnableBlendingBit), (rc.flags & EnableColorWriteBit), useTexturing);
                activeVertexBuffer = changeShaderProgram(activeDefaultEffect, currentColor, camViewPerspMatrix);
            } else {
                activeDefaultEffect = InvalidTextureRef;
                activeVertexBuffer = changeShaderProgram(rc.effectRef, currentColor, camViewPerspMatrix);
            }
        }

        // SETUP TEXTURING
#if SAC_DEBUG
        const TextureRef rrr = rc.texture;
#endif
        const bool rcUseFbo = rc.rflags & RenderingFlags::TextureIsFBO;
        if (rc.texture != InvalidTextureRef) {
            if (!rcUseFbo) {
                const TextureInfo* info = textureLibrary.get(rc.texture, false);

                LOGE_IF(info == 0, "Invalid texture " << rc.texture << "(" << INV_HASH(rc.texture) << ") : can not be found");
                LOGE_IF((unsigned)info->atlasIndex >= atlas.size(), "Invalid atlas index: " << info->atlasIndex << " >= atlas count : " << atlas.size());

                TextureRef aRef = atlas[info->atlasIndex].ref;

                const TextureInfo* atlasInfo;
                if (aRef == previousAtlasRef) {
                    atlasInfo = previousAtlasInfo;
                } else {
                    previousAtlasInfo = atlasInfo = textureLibrary.get(aRef, false);
                    previousAtlasRef = aRef;
                    LOGE_IF(!atlasInfo, "TextureInfo for atlas index: "
                        << info->atlasIndex << " not found (ref=" << aRef << ", name='" << atlas[info->atlasIndex].name << "')");
                }
                rc.glref = atlasInfo->glref;
                computeUV(rc, *info);
            } else {
                rc.uv[0] = glm::vec2(0, 1);
                rc.uv[1] = glm::vec2(1, 0);
                rc.rotateUV = 0;
            }
            if (rc.glref.color == 0)
                rc.glref.color = whiteTexture;
        } else {
            rc.glref = InternalTexture::Invalid;

            if (!(currentFlags & EnableBlendingBit)) {
                rc.glref.color = whiteTexture;
                rc.glref.alpha = whiteTexture;
            }
            rc.uv[0] = glm::vec2(0.0f, 0.0f);
            rc.uv[1] = glm::vec2(1.0f, 1.0f);
            rc.rotateUV = 0;
        }

        // TEXTURE OR COLOR HAS CHANGED ?
        const bool condUseFbo = (useFbo != rcUseFbo);
        const bool condTexture = (!rcUseFbo && boundTexture != rc.glref && (currentFlags & EnableColorWriteBit));
        const bool condFbo = (rcUseFbo && fboRef != rc.framebuffer);
        const bool condColor = (currentColor != rc.color);
        if (condUseFbo | condTexture | condFbo | condColor) {
            #if SAC_DEBUG
            if (condUseFbo) {
                batchSizes.push_back(std::make_pair(BatchFlushReason::NewTarget, batchTriangleCount));
            } else if (condTexture) {
                batchSizes.push_back(std::make_pair(BatchFlushInfo(BatchFlushReason::NewTexture, rrr), batchTriangleCount));
            } else if (condColor) {
                batchSizes.push_back(std::make_pair(BatchFlushInfo(BatchFlushReason::NewColor, rc.color), batchTriangleCount));
            } else if (condColor) {
                batchSizes.push_back(std::make_pair(BatchFlushReason::NewFBO, batchTriangleCount));
            }
            batchTriangleCount = 0;
            #endif
            // flush before changing texture/color
            indiceCount = batchVertexCount = drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);
            if (rcUseFbo) {
                fboRef = rc.framebuffer;
                boundTexture = InternalTexture::Invalid;
            } else {
                fboRef = DefaultFrameBufferRef;
                boundTexture = rc.glref;
#if SAC_INGAME_EDITORS
                if (highLight.nonOpaque)
                    boundTexture.alpha = whiteTexture;
#endif
            }
            useFbo = rcUseFbo;
            if (condTexture) {
                if (currentEffect == DefaultEffectRef) {
                    EffectRef newDefaultCandidate = chooseDefaultShader(currentFlags & EnableBlendingBit, currentFlags & EnableColorWriteBit, (boundTexture != InternalTexture::Invalid));
                    if (newDefaultCandidate != activeDefaultEffect) {
                        activeDefaultEffect = newDefaultCandidate;
                        activeVertexBuffer = changeShaderProgram(activeDefaultEffect, rc.color, camViewPerspMatrix);
                        currentColor = rc.color;
                    }
                }

                /* Map boundTexture (reference) to the glref (real GL texture handles) */
                auto glref = chooseTextures(boundTexture, fboRef, useFbo);

                /* Change texture */
                /*   1. Color texture goes to GL_TEXTURE_0 */
                GL_OPERATION(glActiveTexture(GL_TEXTURE0))
                GL_OPERATION(glBindTexture(GL_TEXTURE_2D, glref.first))
                /*   2. Alpha texture goes to GL_TEXTURE_1 */
                GL_OPERATION(glActiveTexture(GL_TEXTURE1))
                GL_OPERATION(glBindTexture(GL_TEXTURE_2D, glref.second))
            }
            if (currentColor != rc.color) {
                currentColor = rc.color;
                GL_OPERATION(glUniform4fv(activeProgramColorU, 1, currentColor.rgba))
            }
        }

#if SAC_DEBUG
        batchContent.resize(batchSizes.size() + 1);
        batchContent[batchSizes.size() - 1].push_back(rc);
#endif

        // lookup shape
        const Polygon& polygon = theTransformationSystem.shapes[rc.shapeType];

        if (((batchVertexCount + polygon.vertices.size()) >= MAX_VERTEX_COUNT) | ((indiceCount + polygon.indices.size()) >= MAX_INDICE_COUNT)) {
            #if SAC_DEBUG
            batchSizes.push_back(std::make_pair(BatchFlushReason::Full, batchTriangleCount));
            batchTriangleCount = 0;
            #endif
            indiceCount = batchVertexCount = drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);
        }

        // ADD TO BATCH
        LOGF_IF((activeVertexBuffer == Buffers::Dynamic) && (rc.rflags & RenderingFlags::Constant), "Ouch");
        LOGF_IF((activeVertexBuffer == Buffers::Static) && !(rc.rflags & RenderingFlags::Constant), "Ouch2");


        #if SAC_DEBUG
        if (rc.batchIndex) {
            *(rc.batchIndex) = batchSizes.size();
        }
        #endif

        addRenderCommandToBatch(rc,
            polygon,
            vertices + batchVertexCount,
            indices + indiceCount,
            &batchVertexCount,
        #if SAC_DEBUG
         &batchTriangleCount
        #else
         0
         #endif
         , &indiceCount);
    }
    #if SAC_DEBUG
    batchSizes.push_back(std::make_pair(BatchFlushReason::End, batchTriangleCount));
    #endif
    drawBatchES2(vertices, indices, batchVertexCount, indiceCount, activeVertexBuffer);

    #if 0
    FIXME
    static unsigned ______debug = 0;
    if ((++______debug % 3000) == 0) {
        ______debug = 0;
        LOGI("Render command size: " << count << ". Drawn using: " << batchSizes.size() << " batches");
        for (unsigned i=0; i<batchSizes.size(); i++) {
            const auto& p = batchSizes[i];
            LOGI("   # batch " << i << ", size: "<< p.second << ", reason: " << p.first);

            const auto& cnt = batchContent[i];
            for (unsigned j=0; j<cnt.size(); j++) {
                const auto& rc = cnt[j];
                if (!rc.e)
                    continue;

                auto tex = RENDERING(rc.e)->texture;
                LOGI("      > rc " << j << "[" << std::hex << rc.key << "]: '" << theEntityManager.entityName(rc.e)
                    << "', z:" << rc.z << ", flags:" << std::hex << rc.flags << std::dec
                    << ", texture: '" << (tex == InvalidTextureRef ? "None" : theRenderingSystem.textureLibrary.ref2Name(tex))
                    << "', color:" << rc.color);
            }
        }
    }
    batchSizes.clear();
    #endif

#if SAC_ANDROID || SAC_EMSCRIPTEN
    if (hasDiscardExtension) {
        const GLenum discards[] = { GL_DEPTH_EXT };
        // glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        GL_OPERATION(glDiscardFramebufferEXT(GL_FRAMEBUFFER, 1, discards))
    }
#endif

    glState.flags.current = currentFlags;

    #if SAC_DEBUG
    check_GL_errors("Frame end");
    #endif
}

void RenderingSystem::waitDrawingComplete() {
#if ! SAC_EMSCRIPTEN || BENCHMARK_MODE
    PROFILE("Renderer", "wait-drawing-donE", BeginEvent);
    LOG_USAGE_ONLY(int waitOnQueue = currentWriteQueue);
    std::unique_lock<std::mutex> lock(mutexes[L_RENDER]);
    while (newFrameReady && frameQueueWritable) {
        LOGV(3, "Wait for " << waitOnQueue << " to be emptied by rendering thread");
        cond[C_RENDER_DONE].wait(lock);
    }
    lock.unlock();
    PROFILE("Renderer", "wait-drawing-donE", EndEvent);
#endif
}

void RenderingSystem::render() {
#if ! SAC_EMSCRIPTEN
    if (!initDone)
        return;
    PROFILE("Renderer", "wait-frame", BeginEvent);

    std::unique_lock<std::mutex> lock(mutexes[L_QUEUE]);
    // processDelayedTextureJobs();
    while (!newFrameReady && frameQueueWritable) {
        cond[C_FRAME_READY].wait(lock);
    }
    #if SAC_DEBUG
    check_GL_errors("PreFrame");
    #endif
#endif
    int readQueue = (currentWriteQueue + 1) % 2;
    newFrameReady = false;
    if (!frameQueueWritable) {
        LOGV(1, "Rendering disabled");
        renderQueue[readQueue].count = 0;
#if ! SAC_EMSCRIPTEN
        lock.unlock();
#endif
        return;
    }
#if ! SAC_EMSCRIPTEN
    lock.unlock();
    PROFILE("Renderer", "wait-frame", EndEvent);
#endif
    PROFILE("Renderer", "load-textures", BeginEvent);
    processDelayedTextureJobs();
#if SAC_ENABLE_LOG && ! SAC_EMSCRIPTEN
    //float aftertexture= TimeUtil::GetTime();
#endif
    PROFILE("Renderer", "load-textures", EndEvent);
#if ! SAC_EMSCRIPTEN
    if (!mutexes[L_RENDER].try_lock()) {
        LOGV(3, "HMM Busy render lock");
        mutexes[L_RENDER].lock();
    }
#endif

    PROFILE("Renderer", "render", BeginEvent);
    if (renderQueue[readQueue].count == 0) {
        LOGW("Arg, nothing to render - probably a bug (queue=" << readQueue << ')');
    } else {
        RenderQueue& inQueue = renderQueue[readQueue];
        drawRenderCommands(inQueue);
        inQueue.count = 0;
    }
    LOGV(3, "DONE");
    PROFILE("Renderer", "render", EndEvent);
#if SAC_INGAME_EDITORS
    RenderingSystem::ImImpl_RenderDrawLists(ImGui::GetDrawData());
#endif
#if ! SAC_EMSCRIPTEN
    cond[C_RENDER_DONE].notify_all();
    mutexes[L_RENDER].unlock();
#endif
}

static void computeVerticesScreenPos(const std::vector<glm::vec2>& points, const glm::vec2& position, const glm::vec2& hSize, float rotation, float z, VertexData* out) {
    for (unsigned i=0; i<points.size(); i++) {
        out[i].position = glm::vec3(position + glm::rotate(points[i] * (2.0f * hSize), rotation), z);
    }
}

EffectRef RenderingSystem::chooseDefaultShader(bool alphaBlendingOn, bool colorEnabled, bool hasTexture) const {
   if (colorEnabled) {
        if (!alphaBlendingOn) {
            return defaultShaderNoAlpha;
        } else if (hasTexture) {
            return defaultShader;
        } else {
            return defaultShaderNoTexture;
        }
    } else {
        return defaultShaderEmpty;
    }
}

#if SAC_INGAME_EDITORS

// from imgui example
void RenderingSystem::ImImpl_RenderDrawLists(ImDrawData* draw_data) {
    LevelEditor::lock();

    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    GL_OPERATION(glEnable(GL_BLEND));
    GL_OPERATION(glBlendEquation(GL_FUNC_ADD));
    GL_OPERATION(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL_OPERATION(glDisable(GL_CULL_FACE));
    GL_OPERATION(glDisable(GL_DEPTH_TEST));
    GL_OPERATION(glEnable(GL_SCISSOR_TEST));
    GL_OPERATION(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));

    // Setup viewport, orthographic projection matrix
    theRenderingSystem.glState.viewport.update(fb_width, fb_height);
    GL_OPERATION(glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
    theRenderingSystem.glState.clear.update(Color(1, 1, 1, 1));

    GL_OPERATION(glUseProgram(leProgram));
    glm::mat4 mvp;
    mvp = glm::ortho(0.0f, float(fb_width), float(fb_height), 0.0f, 0.0f, 1.0f);
    GL_OPERATION(glUniform1i(leProgramuniformColorSampler, 0));
    GL_OPERATION(glUniformMatrix4fv(leProgramuniformMatrix, 1, GL_FALSE, glm::value_ptr(mvp)));
    GL_OPERATION(glBindSampler(0, 0)); // Rely on combined texture/sampler state.

    // Recreate the VAO every time
    // (This is to easily allow multiple GL contexts. VAO are not shared among GL contexts, and we don't track creation/deletion of windows so we don't have an obvious key to use to cache them.)
    GLuint vao_handle = 0;
    GL_OPERATION(glGenVertexArrays(1, &vao_handle));
    GL_OPERATION(glBindVertexArray(vao_handle));
    GL_OPERATION(glBindBuffer(GL_ARRAY_BUFFER, theRenderingSystem.glBuffers[Buffers::Editor]));
    GL_OPERATION(glEnableVertexAttribArray(0));
    GL_OPERATION(glEnableVertexAttribArray(1));
    GL_OPERATION(glEnableVertexAttribArray(2));
    GL_OPERATION(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos)));
    GL_OPERATION(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv)));
    GL_OPERATION(glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col)));

    // Draw
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        GL_OPERATION(glBindBuffer(GL_ARRAY_BUFFER, theRenderingSystem.glBuffers[Buffers::Editor]));
        GL_OPERATION(glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW));

        GL_OPERATION(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, theRenderingSystem.glBuffers[Buffers::Indice]));
        GL_OPERATION(glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                GL_OPERATION(glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId));
                GL_OPERATION(glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y)));
                GL_OPERATION(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset));
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }
    GL_OPERATION(glDeleteVertexArrays(1, &vao_handle));

    GL_OPERATION(glDisable(GL_SCISSOR_TEST));
    GL_OPERATION(glEnable(GL_DEPTH_TEST));

    // Restore pre-multiplied alpha blending
    GL_OPERATION(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    LevelEditor::unlock();
}

#endif
