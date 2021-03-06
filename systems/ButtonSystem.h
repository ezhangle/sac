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

#pragma once

#include "System.h"

#include "opengl/TextureLibrary.h"

class VibrateAPI;

struct ButtonComponent {
    ButtonComponent()
        : mouseOver(false), clicked(false), lastClick(0.f), enabled(false),
          textureActive(InvalidTextureRef), textureInactive(InvalidTextureRef),
          overSize(1.f), vibration(0.035f), type(NORMAL), trigger(0) {}

    ////// READ ONLY variables
    // States of button
    bool mouseOver;
    bool clicked, touchStartOutside;
    // Last time when entity was clicked (anti-bounced variable)
    float lastClick;
    ////// END OF READ ONLY variables

    ////// READ/WRITE variables
    // if true, entity is clickable
    bool enabled;
    ////// END OF READ/WRITE variables

    ////// WRITE ONLY variables
    // inactive -> all the time
    // active -> texture when the button is touch
    TextureRef textureActive, textureInactive;
    // To make easier to click on the button
    float overSize;
    // Vibration on clicked
    float vibration;
    // type of button
    enum {
        NORMAL,
        LONGPUSH,
    } type;
    // Trigger
    float trigger;
    ////// END OF WRITE ONLY variables

    // first touch
    float firstTouch;
};

#define theButtonSystem ButtonSystem::GetInstance()
#if SAC_DEBUG
#define BUTTON(actor) theButtonSystem.Get(actor, true, __FILE__, __LINE__)
#else
#define BUTTON(actor) theButtonSystem.Get(actor)
#endif
UPDATABLE_SYSTEM(Button)

public:
VibrateAPI* vibrateAPI;

private:
void UpdateButton(Entity entity,
                  ButtonComponent* comp,
                  bool touching,
                  const glm::vec2& touchPos);

#ifdef SAC_DEBUG
std::map<Entity, TextureRef> oldTexture;
#endif
}
;
