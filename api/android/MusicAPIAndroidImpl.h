#pragma once

#include "../MusicAPI.h"
#include <jni.h>

class MusicAPIAndroidImpl : public MusicAPI {
    public:
    	MusicAPIAndroidImpl(JNIEnv *env);
    	
        void init();
        OpaqueMusicPtr* createPlayer(int sampleRate);
        int8_t* allocate(OpaqueMusicPtr* _ptr, int size);
        void queueMusicData(OpaqueMusicPtr* ptr, int8_t* data, int size, int sampleRate);
        int needData(OpaqueMusicPtr* ptr, int sampleRate, bool firstCall);
        void startPlaying(OpaqueMusicPtr* ptr, OpaqueMusicPtr* master, int offset);
        void stopPlayer(OpaqueMusicPtr* ptr);
        int getPosition(OpaqueMusicPtr* ptr);
        void setPosition(OpaqueMusicPtr* ptr, int pos);
        void setVolume(OpaqueMusicPtr* ptr, float v);
        void deletePlayer(OpaqueMusicPtr* ptr);
        bool isPlaying(OpaqueMusicPtr* ptr);
        
	private:
		JNIEnv *env;
		struct MusicAPIAndroidImplData;
		MusicAPIAndroidImplData* datas;
};
