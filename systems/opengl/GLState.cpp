#include "GLState.h"

#if SAC_INGAME_EDITORS
#include "../RenderingSystem.h"
#endif

GLState::GLState() {
    viewport.w = 0;
    viewport.h = 0;
    clear.color = Color(0, 0, 0);
    flags.current = 0;
}

void GLState::Viewport::update(int _w, int _h) {
    if (_w != w || _h != h) {
        w = _w;
        h = _h;
        GL_OPERATION(glViewport(0, 0, w, h))
    }
}

void GLState::Clear::update(const Color& _color) {
    if (_color != color) {
        color = _color;
        GL_OPERATION(glClearColor(color.r, color.g, color.b, color.a))
    }
}

uint32_t GLState::Flags::update(uint32_t bits) {
    const int bitsChanged = current ^ bits;

    if (bitsChanged & EnableZWriteBit ) {
        GL_OPERATION(glDepthMask(bits & EnableZWriteBit))
    }

    // iff EnableBlendingBit changed
    if (bitsChanged & EnableBlendingBit ) {
        if (bits & EnableBlendingBit) {
            #if SAC_INGAME_EDITORS
            if (!theRenderingSystem.wireframe)
            #endif
            GL_OPERATION(glEnable(GL_BLEND))
        } else {
             GL_OPERATION(glDisable(GL_BLEND))
        }
    }

    // iff EnableColorWriteBit changed
    if (bitsChanged & EnableColorWriteBit ) {
        const bool colorMask = bits & EnableColorWriteBit;
        GL_OPERATION(glColorMask(colorMask, colorMask, colorMask, colorMask))
    }

    current = bits;
    return bitsChanged;
}
