/*
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

package io.github.mxvisor.srsdk;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

import java.io.IOException;
import java.io.PrintWriter;

/**
 * The SDLActivity of the game.  UnpackActivity, the launcher's, unpacks the
 * game's data first; started without it (from the list of recent programs
 * after an update of the APK) this one hands over to it.
 */
public class GameActivity extends SDLActivity {
    private String tag = "SR-SDK";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        tag = getString(R.string.app_name);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            writeJavaCrash(throwable);
            if (previous != null) previous.uncaughtException(thread, throwable);
        });
        startup("onCreate");
        if (!UnpackActivity.ready(this)) {
            // finished before its window shows: SDL's surface, and so the
            // native side, never start (onDestroy undoes the rest of SDL's
            // onCreate)
            startup("no data: to UnpackActivity");
            startActivity(new Intent(this, UnpackActivity.class));
            finish();
        }
        startup("super.onCreate");
        super.onCreate(savedInstanceState);
        startup("super.onCreate done");
        hideSystemBars();
    }

    /* SDL asks for the orientation itself when it makes a resizable window (all
     * orientations, FULL_USER), and that overrides the manifest's: the games have
     * a landscape picture and a landscape touch layout only. */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    /* The picture over the whole screen: no status bar, no navigation buttons
     * (a swipe from the edge shows them for a moment), under the camera cut-out. */
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams params = window.getAttributes();
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(params);
        }
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    /* The Java crash and the steps before the native side starts, in files the
     * user can read without adb: Android/data/<package>/files/. */
    private void startup(String message) {
        UnpackActivity.startup(this, message);
    }

    private void writeJavaCrash(Throwable throwable) {
        try (PrintWriter out = new PrintWriter(UnpackActivity.externalFile(this, "crash-java.txt"))) {
            throwable.printStackTrace(out);
        } catch (IOException e) {
            Log.w(tag, "cannot write crash-java.txt", e);
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        startup("onStart done");
    }

    @Override
    protected void onResume() {
        startup("onResume");
        super.onResume();
        startup("onResume done");
    }

    @Override
    protected void onPause() {
        startup("onPause");
        super.onPause();
        startup("onPause done");
    }

    @Override
    protected void onStop() {
        startup("onStop");
        super.onStop();
        startup("onStop done");
    }

    @Override
    protected void onDestroy() {
        startup("onDestroy");
        super.onDestroy();
    }

    /* libmain.so needs the others; before Android 6 the linker does not look
     * for them in the app's own library directory. */
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "SDL2_mixer", "main" };
    }

    private static native String nativeLoadMain(String path);

    /* libmain.so goes where its code pointers reach the guest's memory, see
     * gameloader.c; SDL's own dlopen of it then finds it loaded. */
    @Override
    public void loadLibraries() {
        System.loadLibrary("gameloader");
        for (String library : getLibraries()) {
            if (!library.equals("main")) {
                SDL.loadLibrary(library, this);
            }
        }
        String error = nativeLoadMain(getApplicationInfo().nativeLibraryDir + "/libmain.so");
        if (error != null) {
            throw new UnsatisfiedLinkError(error);
        }
    }
}
