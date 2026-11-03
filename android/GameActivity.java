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

import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
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

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

/**
 * SDLActivity that unpacks the game's data before the native side starts:
 * the port reads everything with fopen, which cannot see APK assets.
 *
 * Layout after the first launch:
 *   files/game/   the game's data from the APK's assets/game/
 *   files/write/  the write root: saves, screenshots, the port's log
 *
 * Game_android.c hands both to the port.  The copy goes to game.staging and
 * is renamed into place, so a failed or interrupted run never leaves a
 * half-extracted game/ behind; game/.installed holds the time the APK was
 * installed, so an updated APK unpacks its data again.
 */
public class GameActivity extends SDLActivity {
    private static final int BUFFER = 64 * 1024;
    private static final String STAMP = ".installed";

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
        try {
            prepareData();
            startup("data ready");
        } catch (IOException e) {
            startup("data failed: " + e);
            Log.e(tag, "cannot unpack the game's data", e);
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

    /* A file the user can read without adb: Android/data/<package>/files/.
     * The Java crash and the steps before the native side starts. */
    private File externalFile(String name) {
        File dir = getExternalFilesDir(null);
        return new File(dir != null ? dir : getFilesDir(), name);
    }

    private void startup(String message) {
        try (PrintWriter out = new PrintWriter(new FileWriter(externalFile("startup.txt"), true))) {
            out.println(System.currentTimeMillis() + " " + message);
        } catch (IOException e) {
            Log.w(tag, "cannot write startup.txt", e);
        }
    }

    private void writeJavaCrash(Throwable throwable) {
        try (PrintWriter out = new PrintWriter(externalFile("crash-java.txt"))) {
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

    private void prepareData() throws IOException {
        File files = getFilesDir();
        File game = new File(files, "game");
        String installed = installTime();
        new File(files, "write").mkdirs();
        if (installed.equals(readStamp(new File(game, STAMP)))) {
            return;
        }
        long start = System.currentTimeMillis();
        File staging = new File(files, "game.staging");
        deleteRecursively(staging);
        copyTree(getAssets(), "game", staging);
        try (OutputStream out = new FileOutputStream(new File(staging, STAMP))) {
            out.write(installed.getBytes(StandardCharsets.UTF_8));
        }
        deleteRecursively(game);
        if (!staging.renameTo(game)) {
            throw new IOException("cannot rename " + staging + " to " + game);
        }
        Log.i(tag, "game data ready in " + (System.currentTimeMillis() - start) + " ms");
    }

    private String installTime() {
        try {
            return Long.toString(getPackageManager()
                    .getPackageInfo(getPackageName(), 0).lastUpdateTime);
        } catch (PackageManager.NameNotFoundException e) {
            return "";
        }
    }

    private static String readStamp(File stamp) {
        byte[] buffer = new byte[64];
        try (InputStream in = new FileInputStream(stamp)) {
            int read = in.read(buffer);
            return read > 0 ? new String(buffer, 0, read, StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return null;
        }
    }

    /** Copies the asset tree below assetPath to destination (file or dir). */
    private static void copyTree(AssetManager assets, String assetPath, File destination)
            throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("cannot create " + destination);
            }
            for (String name : children) {
                copyTree(assets, assetPath + "/" + name, new File(destination, name));
            }
            return;
        }
        // An empty list is either a file or an empty directory; only the
        // open() tells them apart, so a directory failing to open becomes one.
        InputStream in = null;
        try {
            in = assets.open(assetPath, AssetManager.ACCESS_STREAMING);
        } catch (IOException e) {
            if (destination.isDirectory() || destination.mkdirs()) {
                return;
            }
            throw e;
        }
        try (OutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[BUFFER];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        } finally {
            in.close();
        }
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }
        file.delete();
    }
}
