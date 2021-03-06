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



package net.damsy.soupeaucaillou.api;

import android.content.Context;

public class StorageAPI {	
	private static StorageAPI instance = null;
	public static int id = 419223939; /* MurmurHash of 'StorageAPI' */

	public synchronized static StorageAPI Instance() {
		if (instance == null) {
			instance = new StorageAPI();
		}
		return instance;
	}
	
	private String databasePath;
	
	public void init(Context c) {
		databasePath = c.getFilesDir().getName();
	}
	
	public String getDatabasePath() {
		return databasePath;
	}
}
