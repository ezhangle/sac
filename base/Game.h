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

#include <stdint.h>
#include <vector>
#include <set>
#include "GameContext.h"
#include "base/Entity.h"
#include "util/Tuning.h"

class AssetApi;
class ComponentSystem;
#if !SAC_MOBILE
class MouseNativeTouchState;
#endif

#if SAC_DEBUG
#define FPS_DEBUG_N 120
#endif

#if SAC_INGAME_EDITORS

class LevelEditor;

namespace GameType {
    enum Enum { Default, LevelEditor, SingleStep, Replay };
}
#endif

class Game {
    public:
    virtual void init(const uint8_t* in = 0, int size = 0) = 0;

    private:
    virtual void tick(float dt) = 0;

    public:
    Game();
    virtual ~Game();

    void setGameContexts(GameContext* gameThreadContext,
                         GameContext* renderThreadContext);
    void step();
    void render();
    void resetTime();
    void eventsHandler();
    /* Override to be able to react on resolution changes */
    virtual void changeResolution(int windowW, int windowH);

    virtual bool isLandscape() const { return true; }
    virtual bool wantsAPI(ContextAPI::Enum api) const;
    virtual void backPressed(){};
    virtual bool willConsumeBackEvent() { return false; }
    virtual void quickInit(){};
    virtual int saveState(uint8_t** out);
    virtual void sacInitFromGameThread();
    virtual void sacInitFromRenderThread();
    virtual void togglePause(bool) {}
    virtual void preDestroy();

    protected:
    void loadFont(AssetAPI* asset, const char* name);

    public:
    GameContext* gameThreadContext, *renderThreadContext;
    Entity camera;

#if !SAC_MOBILE
    // tough... only needed for mouse events handling
    MouseNativeTouchState* mouseNativeTouchState;
    struct {
        int c;
        char** v;
    } arg;
#endif

    float targetDT;
    Tuning tuning;

    bool isFinished;

    struct {
        float minDt, maxDt;
        float since;
        int frameCount;
        void reset(float timeMark) {
            minDt = 10000000;
            maxDt = 0;
            since = timeMark;
            frameCount = 0;
        }
#if SAC_DEBUG
        Entity square[FPS_DEBUG_N];
        Entity text[FPS_DEBUG_N];
#endif
    } fpsStats;
    float lastUpdateTime;
#if SAC_INGAME_EDITORS
    GameType::Enum gameType;
    LevelEditor* levelEditor;
#endif
    public:
    void buildOrderedSystemsToUpdateList();

    public:
    std::vector<ComponentSystem*> orderedSystemsToUpdate;

#if SAC_ENABLE_LOG
    std::set<ComponentSystem*> unusedSystems;
#endif

};
