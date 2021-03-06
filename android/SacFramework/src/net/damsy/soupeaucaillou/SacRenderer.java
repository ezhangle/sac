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



package net.damsy.soupeaucaillou;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import net.damsy.soupeaucaillou.SacGameThread.Event;
import android.content.pm.ActivityInfo;
import android.opengl.GLSurfaceView;

public class SacRenderer implements GLSurfaceView.Renderer {
	SacActivity activity;
	final Thread gameThread;
	final SacGameThread sacGame;
	long time;
	int width, height;
	int densityDpi;

	public SacRenderer(SacActivity act, int width, int height, SacGameThread sacG, int densityDpi, int requestedOrientation) {
		super();
		this.activity = act;
		this.sacGame = sacG;
		this.gameThread = new Thread(this.sacGame, "GameUpdate");
		this.time = System.currentTimeMillis();
		if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT) {
			this.height = Math.max(width, height);
			this.width = Math.min(width, height);
		} else {
			this.width = Math.max(width, height);
			this.height = Math.min(width, height);
		}
		sacGame.setResolution(width, height);
		this.densityDpi = densityDpi;
		SacActivity.Log(SacActivity.I, "SacRenderer created w,h=" + width + "," + height);
	}

    public void onDrawFrame(GL10 gl) {
    	SacJNILib.render();
    }

    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
    	SacActivity.Log(SacActivity.W, "onSurfaceCreated");
    	// Create (or reset) native game
    	if (SacJNILib.createGame()) {
    		activity.initRequiredAPIs();
    		activity = null;

    		SacJNILib.initFromRenderThread(densityDpi, width, height);
    	} else {
    		// Clear saved state if native game is not recreated
    		sacGame.clearSavedState();
    		SacJNILib.initAndReloadTextures();
    	}

    	if (!gameThread.isAlive()) {
    		SacActivity.Log(SacActivity.I, "Start game thread");
    		gameThread.start();
    	} else {
    		sacGame.postEvent(Event.Resume);
    	}
    }

    public void onSurfaceChanged(GL10 gl, final int w, final int h) {
    	SacActivity.Log(SacActivity.I, "surface changed-> width: " + w + ", height: " + h);
    	width = w;
    	height = h;
    	sacGame.setResolution(w, h);
    	sacGame.postEvent(Event.ResolutionChanged);
    }
}
