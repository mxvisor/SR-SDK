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

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The launcher's activity: unpacks the game's data before GameActivity starts
 * the native side (the port reads everything with fopen, which cannot see APK
 * assets), on a thread of its own, with its progress on the screen.  On the
 * activity's thread the copy (hundreds of MB, seconds) would make Android close
 * the program as one that does not answer.  With the data in place it only
 * starts GameActivity.
 *
 * Layout after the first launch:
 *   files/game/   the game's data from the APK's assets/game/
 *   files/write/  the write root: saves, screenshots, the port's log
 *
 * Game_android.c hands both to the port.  The copy goes to game.staging and
 * is renamed into place, so a failed or interrupted run never leaves a
 * half-extracted game/ behind; game/.installed holds the time the APK was
 * installed, so an updated APK unpacks its data again.  assets/game.files
 * (tools/android_apk.py) lists the tree: "d <path>" for a directory,
 * "<size> <path>" for a file.
 */
public class UnpackActivity extends Activity {
    private static final int BUFFER = 256 * 1024;
    private static final String STAMP = ".installed";
    private static final String LIST = "game.files";

    /* one copy for the process: an activity made again (the user left and came
     * back) shows the progress of the running one */
    private static Thread worker;
    private static volatile String status = "";
    private static volatile boolean done;

    private TextView text;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (done) {
                startGame();
                return;
            }
            text.setText(getString(R.string.app_name) + "\n\n" + status);
            handler.postDelayed(this, 200);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        startup(this, "unpack onCreate");
        if (ready(this)) {
            startGame();
            return;
        }
        text = new TextView(this);
        text.setBackgroundColor(Color.BLACK);
        text.setTextColor(Color.WHITE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        text.setGravity(Gravity.CENTER);
        setContentView(text);
        synchronized (UnpackActivity.class) {
            if (worker == null) {
                done = false;
                status = "Unpacking the game's data";
                final Context context = getApplicationContext();
                worker = new Thread(() -> unpack(context), "unpack");
                worker.start();
            }
        }
    }

    /* the screen follows the copy only while it is seen: GameActivity is not
     * started from the background */
    @Override
    protected void onResume() {
        super.onResume();
        if (text != null) handler.post(poll);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(poll);
        super.onPause();
    }

    private void startGame() {
        startup(this, "data ready");
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private static void unpack(Context context) {
        try {
            prepareData(context);
            synchronized (UnpackActivity.class) {
                worker = null;
                done = true;
            }
        } catch (IOException e) {
            Log.e(tag(context), "cannot unpack the game's data", e);
            startup(context, "data failed: " + e);
            status = "Cannot unpack the game's data:\n" + e.getMessage();
            synchronized (UnpackActivity.class) {
                worker = null;
            }
        }
    }

    /** The data of this APK is in files/game/. */
    static boolean ready(Context context) {
        File game = new File(context.getFilesDir(), "game");
        return installTime(context).equals(readStamp(new File(game, STAMP)));
    }

    private static void prepareData(Context context) throws IOException {
        File files = context.getFilesDir();
        File game = new File(files, "game");
        String installed = installTime(context);
        new File(files, "write").mkdirs();
        long start = System.currentTimeMillis();

        AssetManager assets = context.getAssets();
        List<String> dirs = new ArrayList<>();
        List<String> paths = new ArrayList<>();
        List<Long> sizes = new ArrayList<>();
        long total = 0;
        try (BufferedReader in = new BufferedReader(new InputStreamReader(
                assets.open(LIST), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                int space = line.indexOf(' ');
                if (space <= 0) continue;
                String kind = line.substring(0, space);
                String path = line.substring(space + 1);
                if (kind.equals("d")) {
                    dirs.add(path);
                } else {
                    long size = Long.parseLong(kind);
                    paths.add(path);
                    sizes.add(size);
                    total += size;
                }
            }
        }

        File staging = new File(files, "game.staging");
        deleteRecursively(staging);
        // what the old data leaves free counts: it goes before the rename
        long free = files.getUsableSpace() + sizeOf(game);
        if (free < total) {
            throw new IOException("not enough free space: " + mb(total) + " MB needed, "
                    + mb(free) + " MB free");
        }
        if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
        for (String dir : dirs) {
            File target = new File(staging, dir);
            if (!target.isDirectory() && !target.mkdirs()) {
                throw new IOException("cannot create " + target);
            }
        }
        byte[] buffer = new byte[BUFFER];
        long copied = 0;
        int percent = -1;
        for (int index = 0; index < paths.size(); index++) {
            File target = new File(staging, paths.get(index));
            File parent = target.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("cannot create " + parent);
            }
            try (InputStream in = assets.open("game/" + paths.get(index), AssetManager.ACCESS_STREAMING);
                 OutputStream out = new FileOutputStream(target)) {
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                    copied += read;
                    int now = total > 0 ? (int) (copied * 100 / total) : 100;
                    if (now != percent) {
                        percent = now;
                        status = "Unpacking the game's data: " + percent + " %\n"
                                + mb(copied) + " of " + mb(total) + " MB\n\n"
                                + "(only after an install or an update)";
                    }
                }
            }
        }
        try (OutputStream out = new FileOutputStream(new File(staging, STAMP))) {
            out.write(installed.getBytes(StandardCharsets.UTF_8));
        }
        deleteRecursively(game);
        if (!staging.renameTo(game)) {
            throw new IOException("cannot rename " + staging + " to " + game);
        }
        Log.i(tag(context), "game data ready in " + (System.currentTimeMillis() - start) + " ms");
    }

    private static long mb(long bytes) {
        return bytes / (1024 * 1024);
    }

    private static long sizeOf(File file) {
        if (!file.isDirectory()) return file.length();
        long size = 0;
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) size += sizeOf(child);
        }
        return size;
    }

    private static String installTime(Context context) {
        try {
            return Long.toString(context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0).lastUpdateTime);
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

    private static String tag(Context context) {
        return context.getString(R.string.app_name);
    }

    /* A file the user can read without adb: Android/data/<package>/files/. */
    static File externalFile(Context context, String name) {
        File dir = context.getExternalFilesDir(null);
        return new File(dir != null ? dir : context.getFilesDir(), name);
    }

    /* The steps before the native side starts, in startup.txt. */
    static void startup(Context context, String message) {
        try (PrintWriter out = new PrintWriter(new FileWriter(externalFile(context, "startup.txt"), true))) {
            out.println(System.currentTimeMillis() + " " + message);
        } catch (IOException e) {
            Log.w(tag(context), "cannot write startup.txt", e);
        }
    }
}
