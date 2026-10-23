package com.gamestudio.nova;

import org.libsdl.app.SDLActivity;

/** Hosts a Nova game: SDL's activity, loading libSDL3.so and the game's libmain.so. */
public class NovaActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}
