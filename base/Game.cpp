#include "Game.h"

#include <base/EntityManager.h>
#include "systems/TransformationSystem.h"
#include "systems/RenderingSystem.h"
#include "systems/ButtonSystem.h"
#include "systems/ADSRSystem.h"
#include "systems/TextRenderingSystem.h"
#include "systems/SoundSystem.h"
#include "systems/MusicSystem.h"
#include "systems/ContainerSystem.h"
#include "systems/PhysicsSystem.h"
#include "systems/ParticuleSystem.h"
#include "systems/ScrollingSystem.h"
#include "systems/MorphingSystem.h"
#include "systems/AutonomousAgentSystem.h"
#include "systems/AnimationSystem.h"
#include "systems/NetworkSystem.h"
#include "systems/AutoDestroySystem.h"
#include "systems/CameraSystem.h"
#include "systems/GraphSystem.h"
#include "systems/DebuggingSystem.h"
#include "api/AssetAPI.h"
#include "base/PlacementHelper.h"
#include "base/TouchInputManager.h"
#include "base/Profiler.h"
#include "util/DataFileParser.h"
#include <sstream>

#ifdef SAC_INGAME_EDITORS
#include <GL/glfw.h>
#endif

Game::Game() {
#ifdef SAC_INGAME_EDITORS
    gameType = GameType::Default;
#endif
    targetDT = 1.0f / 60.0f;

    TimeUtil::Init();

	/* create EntityManager */
	EntityManager::CreateInstance();

	/* create systems singleton */
	TransformationSystem::CreateInstance();
	RenderingSystem::CreateInstance();
	SoundSystem::CreateInstance();
    MusicSystem::CreateInstance();
	ADSRSystem::CreateInstance();
	ButtonSystem::CreateInstance();
	TextRenderingSystem::CreateInstance();
	ContainerSystem::CreateInstance();
	PhysicsSystem::CreateInstance();
    ParticuleSystem::CreateInstance();
    ScrollingSystem::CreateInstance();
    MorphingSystem::CreateInstance();
    AutonomousAgentSystem::CreateInstance();
    AnimationSystem::CreateInstance();
    AutoDestroySystem::CreateInstance();
    CameraSystem::CreateInstance();
    GraphSystem::CreateInstance();
    DebuggingSystem::CreateInstance();

#ifdef SAC_NETWORK
    NetworkSystem::CreateInstance();
#endif

    fpsStats.reset(0);
    lastUpdateTime = TimeUtil::GetTime();
#ifdef SAC_INGAME_EDITORS
    levelEditor = new LevelEditor();
#endif
}

Game::~Game() {
    EntityManager::DestroyInstance();
    TransformationSystem::DestroyInstance();
    RenderingSystem::DestroyInstance();
    SoundSystem::DestroyInstance();
    MusicSystem::DestroyInstance();
    ADSRSystem::DestroyInstance();
    ButtonSystem::DestroyInstance();
    TextRenderingSystem::DestroyInstance();
    ContainerSystem::DestroyInstance();
    PhysicsSystem::DestroyInstance();
    ParticuleSystem::DestroyInstance();
    ScrollingSystem::DestroyInstance();
    MorphingSystem::DestroyInstance();
    AutonomousAgentSystem::DestroyInstance();
    AnimationSystem::DestroyInstance();
    AutoDestroySystem::DestroyInstance();
    CameraSystem::DestroyInstance();
    GraphSystem::DestroyInstance();
	DebuggingSystem::DestroyInstance();

#ifdef SAC_NETWORK
    NetworkSystem::DestroyInstance();
#endif
}

void Game::setGameContexts(GameContext* pGameThreadContext, GameContext* pRenderThreadContext) {
    gameThreadContext = pGameThreadContext;
    renderThreadContext = pRenderThreadContext;
}

void Game::loadFont(AssetAPI* asset, const std::string& name) {
	FileBuffer file = asset->loadAsset(name + ".font");
    DataFileParser dfp;
    if (!dfp.load(file)) {
        LOGE("Invalid font description file: " << name)
        return;
    }

    unsigned defCount = dfp.sectionSize(DataFileParser::GlobalSection);
    LOGW_IF(defCount == 0, "Font definition '" << name << "' has no entry")
    std::map<uint32_t, float> h2wratio;
    std::string charUnicode;
    for (unsigned i=0; i<defCount; i++) {
        int w_h[2];
        if (!dfp.get(DataFileParser::GlobalSection, i, charUnicode, w_h, 2)) {
            LOGE("Unable to parse entry #" << i << " of " << name)
        }
        std::stringstream ss;
        ss << std::hex << charUnicode;
        uint32_t cId;
        ss >> cId;
        h2wratio[cId] = (float)w_h[0] / w_h[1];
        LOGV(2, "Font entry: " << cId << ": " << h2wratio[cId])
    }
	delete[] file.data;
	// h2wratio[' '] = h2wratio['r'];
	// h2wratio[0x97] = 1;
	theTextRenderingSystem.registerFont(name, h2wratio);
    LOGI("Loaded font: " << name << ". Found: " << h2wratio.size() << " entries")
}

void Game::sacInit(int windowW, int windowH) {
#ifdef SAC_ENABLE_PROFILING
	initProfiler();
#endif

    if (windowW < windowH) {
	    PlacementHelper::ScreenHeight = 10;
        PlacementHelper::ScreenWidth = PlacementHelper::ScreenHeight * windowW / (float)windowH;
    } else {
        PlacementHelper::ScreenWidth = 20;
        PlacementHelper::ScreenHeight = PlacementHelper::ScreenWidth * windowH / (float)windowW;
    }
    PlacementHelper::WindowWidth = windowW;
    PlacementHelper::WindowHeight = windowH;
    PlacementHelper::GimpWidth = 800.0f;
    PlacementHelper::GimpHeight = 1280.0f;

	theRenderingSystem.setWindowSize(windowW, windowH, PlacementHelper::ScreenWidth, PlacementHelper::ScreenHeight);
	theTouchInputManager.init(Vector2(PlacementHelper::ScreenWidth, PlacementHelper::ScreenHeight), Vector2(windowW, windowH));

	theRenderingSystem.init();
    theRenderingSystem.setFrameQueueWritable(true);
}

void Game::backPressed() {
	#ifdef SAC_ENABLE_PROFILING
	static int profStarted = 0;
	if ((profStarted % 2) == 0) {
		startProfiler();
	} else {
		std::stringstream a;
		#ifdef SAC_ANDROID
		a << "/sdcard/";
		#else
		a << "/tmp/";
		#endif
		a << "sac_prof_" << (int)(profStarted / 2) << ".json";
		stopProfiler(a.str());
	}
	profStarted++;
	#endif
}

int Game::saveState(uint8_t**) {
    theRenderingSystem.setFrameQueueWritable(false);
	return 0;
}

const float DDD = 1.0/60.f;
float countD = 0;
void Game::step() {
    PROFILE("Game", "step", BeginEvent);

    theRenderingSystem.waitDrawingComplete();

    float timeBeforeThisStep = TimeUtil::GetTime();
    float delta = timeBeforeThisStep - lastUpdateTime;

    theTouchInputManager.Update(delta);
    #ifdef SAC_ENABLE_PROFILING
    std::stringstream framename;
    framename << "update-" << (int)(delta * 1000000);
    PROFILE("Game", framename.str(), InstantEvent);
    #endif
    // update game state
    #ifdef SAC_INGAME_EDITORS
    static float speedFactor = 1.0f;
    if (glfwGetKey(GLFW_KEY_F1))
        gameType = GameType::Default;
    else if (glfwGetKey(GLFW_KEY_F2))
        gameType = GameType::LevelEditor;
    switch (gameType) {
        case GameType::LevelEditor:
            levelEditor->tick(delta);
            delta = 0;
            break;
        default:
            if (/*glfwGetKey(GLFW_KEY_KP_ADD) ||*/ glfwGetKey(GLFW_KEY_F6)) {
                speedFactor += 1 * delta;
            } else if (/*glfwGetKey(GLFW_KEY_KP_SUBTRACT) ||*/ glfwGetKey(GLFW_KEY_F5)) {
                speedFactor = MathUtil::Max(speedFactor - 1 * delta, 0.0f);
            } else if (glfwGetKey(GLFW_KEY_KP_ENTER)) {
                speedFactor = 1;
            }
            delta *= speedFactor;
            tick(delta);
    }
    #else
    LOGI("Delta: " << delta);
    tick(delta);
    #endif

    #ifdef SAC_INGAME_EDITORS
    if (delta > 0) {
    #endif
    #ifdef SAC_NETWORK
    theNetworkSystem.Update(delta);
    #endif

	++countD;

#if 0
    // Update FPS graph data
	theDebuggingSystem.addValue(DebuggingSystem::fpsGraphEntity, std::make_pair(countD, 1/delta));
    // Update entity count data
	theDebuggingSystem.addValue(DebuggingSystem::entityGraphEntity, std::make_pair(countD, theEntityManager.getNumberofEntity()));

#endif

    theCameraSystem.Update(delta);
    theADSRSystem.Update(delta);
    theAnimationSystem.Update(delta);
    theButtonSystem.Update(delta);
    theAutonomousAgentSystem.Update(delta);
    theMorphingSystem.Update(delta);
    thePhysicsSystem.Update(delta);
    theScrollingSystem.Update(delta);
    theSoundSystem.Update(delta);
    theMusicSystem.Update(delta);
    theTextRenderingSystem.Update(delta);
    theTransformationSystem.Update(delta);
    theParticuleSystem.Update(delta);
    theContainerSystem.Update(delta);
    theAutoDestroySystem.Update(delta);
    theDebuggingSystem.Update(delta);
    theGraphSystem.Update(delta);
    #ifdef SAC_INGAME_EDITORS
    } else {
        theTransformationSystem.Update(delta);
    }
    #endif
    // produce 1 new frame
    theRenderingSystem.Update(0);

    float updateDuration = TimeUtil::GetTime() - timeBeforeThisStep;
    if (updateDuration < 0.016) {
        //TimeUtil::Wait(0.016 - updateDuration);
    }
    lastUpdateTime = timeBeforeThisStep;

    PROFILE("Game", "step", EndEvent);
}

void Game::render() {
    PROFILE("Game", "render-game", BeginEvent);
    theRenderingSystem.render();

    {
        static int count = 0;
        static float prevT = 0;
        float t = TimeUtil::GetTime();
        float dt = t - prevT;
        prevT = t;

        if (dt > fpsStats.maxDt) {
            fpsStats.maxDt = dt;
        }
        if (dt < fpsStats.minDt) {
            fpsStats.minDt = dt;
        }
        count++;
        if (count == 1000) {
            LOGI("FPS avg/min/max : " <<
                (1000.0 / (t - fpsStats.since)) << '/' << (1.0 / fpsStats.maxDt) << '/' << (1.0 / fpsStats.minDt))
            count = 0;
            fpsStats.reset(t);
        }
    }
    PROFILE("Game", "render-game", EndEvent);
}

void Game::resetTime() {
    fpsStats.reset(TimeUtil::GetTime());
    lastUpdateTime = TimeUtil::GetTime();
}
