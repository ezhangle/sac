sac
===

*sac* is [Soupe au Caillou](http://soupeaucaillou)'s game engine, featuring:
* C++, OpenGL, cmake
* multiplatform: Android, Linux, Mac, Windows and HTML5 (using [emscripten](http://emscripten.org/))
* Entity/System paradigm
* in game editor (a simple one though)

Quick Start (Linux)
-------------------

* Clone repository

```
mkdir mygame && cd mygame
git clone --recursive  git@github.com:SoupeauCaillou/sac.git
```

* Create minimum CMakeList.txt file
```
cmake_minimum_required(VERSION 2.8)
project(Test)
include(sac/build/cmake/CMakeLists.txt)
```

* Add a simple class (sources/TestGame.h)
```C++
#pragma once
#include "base/Game.h"

class TestGame : public Game {
    public:
        void init(const uint8_t*, int) {}
        void tick(float) {}
};
```

* Build:
```
mkdir build
cd build
cmake ..
make
```

* And run:
```
./TestGame
```

You should get something like this:

![screenshot 1](http://soupeaucaillou.com/screenshots/screenshot_proto1.jpg)

Concepts
--------
*sac* uses Entity/System for almost everything. If you want to draw something, you'll create an Entity with a RenderingComponent (from Rendering System). If you need an game object falling because of gravity you'll want to use the PhysicsSystem etc...

An entity is an id, a component is data-store for a system and a system is where the processing happen.

Let's try to make our TestGame a little more impressive.

```C++
/* Necessary includes */
#include "PrototypeGame.h"
#include "base/EntityManager.h"

/* All systems live in the systems/ subfolder */
#include "systems/TransformationSystem.h"
#include "systems/RenderingSystem.h"
#include "systems/PhysicsSystem.h"

void TestGame::init(const uint8_t*, int) {
    /* Create an entity */
    Entity falling = theEntityManager.CreateEntity(HASH("falling", 0x82f4eb06));
    /* Add the TransformationComponent to 'falling' entity */
    ADD_COMPONENT(falling, Transformation);
    /* TransformationComponent has a few properties, but
       we're only going to specify position */
    TRANSFORM(falling)->position = glm::vec2(4, 3);
    /* Now add a RenderingComponent */
    ADD_COMPONENT(falling, Rendering);
    /* And make the entity red... */
    RENDERING(falling)->color = Color(1.0f, 0, 0);
    /* ... and visible */
    RENDERING(falling)->show = true;
    /* Lastly, add a PhysicsComponent */
    ADD_COMPONENT(falling, Physics);
    /* With some basic properties */
    PHYSICS(falling)->mass = 1.0f;
    PHYSICS(falling)->gravity = glm::vec2(0, -1.0f);
}
```

Now if you run the game again, you should get something similar to:

![screenshot 2](http://soupeaucaillou.com/screenshots/screenshot_proto2.jpg)

Notice that on the right of the screen, you have the list of all entities. You can browse them, see which components they have, modify values, etc. You can also see that the camera itself is an entity (with 2 components: Transformation and Camera).

Hashes
------
*sac* tries to limit the use of string (both char* and std::string) at runtime, so most of the time it uses an hash_t type instead. To keep things more user-readable, in debug-build we have a hash-lookup mechanism to display proper strings instead of cryptic hashcode.

In the example above, the entity name given to the CreateEntity method is:
```C++
HASH("falling", 0x82f4eb06)
```
Which is a simple *sac* macro to handle offline hash computation. See [this article](http://bitsquid.blogspot.fr/2010/10/static-hash-values.html) for more background on the topic (and *constexpr* from c++11 couldn't be used because it seems there's no way to force the compiler to evaluate a constexpr)

Systems
-------
TBD: document each system, or at least a small usage example for each

