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



#include "TimeUtil.h"
#include <time.h>

#if SAC_WINDOWS
#include "Mmsystem.h"
#undef ERROR
#endif

#include "Log.h"

#if SAC_LINUX || SAC_ANDROID
    struct timespec TimeUtil::startup_time;
#elif SAC_EMSCRIPTEN || SAC_DARWIN || SAC_IOS
    struct timeval TimeUtil::startup_time;
#elif SAC_WINDOWS
        __int64 TimeUtil::startup_time;
        double TimeUtil::frequency;
#endif

void TimeUtil::Init() {
#if SAC_LINUX || SAC_ANDROID
        clock_gettime(CLOCK_MONOTONIC, &startup_time);
#elif SAC_EMSCRIPTEN || SAC_DARWIN || SAC_IOS
    gettimeofday(&startup_time, 0);
#elif SAC_WINDOWS
    timeBeginPeriod(1);
        QueryPerformanceCounter((LARGE_INTEGER*)&startup_time);
        __int64 invertfrequency;
        QueryPerformanceFrequency((LARGE_INTEGER*)&invertfrequency);
        frequency = 1.0 / invertfrequency;
#endif
}

#if SAC_LINUX || SAC_ANDROID
static inline float timeconverter(const struct timespec & tv) {
        return tv.tv_sec + (float)(tv.tv_nsec) / 1000000000.0f;
}
#elif SAC_EMSCRIPTEN || SAC_DARWIN || SAC_IOS
static inline float timeconverter(const struct timeval & tv) {
    return (tv.tv_sec + tv.tv_usec / 1000000.0f);
}
#elif SAC_WINDOWS
static inline float timeconverter(float tv) {
        return (tv);
}
#endif



#if SAC_LINUX || SAC_ANDROID
static inline void sub(struct timespec& tA, const struct timespec& tB)
{
    if ((tA.tv_nsec - tB.tv_nsec) < 0) {
        tA.tv_sec = tA.tv_sec - tB.tv_sec - 1;
        tA.tv_nsec = 1000000000 + tA.tv_nsec - tB.tv_nsec;
    } else {
        tA.tv_sec = tA.tv_sec - tB.tv_sec;
        tA.tv_nsec = tA.tv_nsec - tB.tv_nsec;
    }
}
#endif

float TimeUtil::GetTime() {
#if SAC_LINUX || SAC_ANDROID
                struct timespec tv;
                if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) {
        LOGF("clock_gettime failure");
                }
                sub(tv, startup_time);
#elif SAC_EMSCRIPTEN || SAC_DARWIN || SAC_IOS
                struct timeval tv;
                gettimeofday(&tv, 0);
                timersub(&tv, &startup_time, &tv);
#elif SAC_WINDOWS
                __int64 intv;
                QueryPerformanceCounter((LARGE_INTEGER*)&intv);
        intv -= startup_time;
                double tv = intv * frequency;
#endif
        return timeconverter(tv);
}

void TimeUtil::Wait(float waitInSeconds) {
       LOGW_IF(waitInSeconds >= 1, "TODO, handle sleep request >= 1 s");
       float before = GetTime();
       float delta = 0;
       while (delta < waitInSeconds) {
#if SAC_LINUX || SAC_ANDROID || SAC_DARWIN || SAC_EMSCRIPTEN
           struct timespec ts;
           ts.tv_sec = 0;
           ts.tv_nsec = (waitInSeconds - delta) * 1000000000LL;
           nanosleep(&ts, 0);
#elif SAC_WINDOWS
           // Of course using Sleep is bad, but hey...
           Sleep((waitInSeconds - delta) * 1000);
#endif
           delta = GetTime() - before;
       }
}
