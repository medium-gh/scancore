package com.mediumdev.scancore;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.media.AudioManager;
import android.provider.Settings;
import android.os.Build;
import android.view.Display;
import android.view.OrientationEventListener;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.WindowInsetsController;

// Turning the phone for the anime player's full screen (src/app/Platform.cpp,
// WatchPage.qml), keeping the screen on, the bars' icons, and the player's
// brightness and volume swipes.
public final class Screen {
    private Screen() {}

    private static OrientationEventListener release;

    // The screen stays on while a video plays (src/app/KeepAwake.cpp).
    public static void keepOn(final Activity activity, final boolean on) {
        activity.runOnUiThread(() -> {
            if (on) activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        });
    }

    // The screen's fastest refresh rate (90, 120, 144 Hz) at its current
    // resolution, or the phone's own choice. Some phones (vivo's OriginOS,
    // OnePlus) keep apps drawn into a SurfaceView, as Qt's are, at 60 Hz
    // unless they ask; scrolling then looks laggy next to other apps (#22).
    public static void fastestRefresh(final Activity activity, final boolean on) {
        if (Build.VERSION.SDK_INT < 23) return;
        activity.runOnUiThread(() -> {
            Window window = activity.getWindow();
            Display display = Build.VERSION.SDK_INT >= 30 ? activity.getDisplay() : activity.getWindowManager().getDefaultDisplay();
            if (window == null || display == null) return;
            int id = 0;
            if (on) {
                Display.Mode current = display.getMode();
                Display.Mode best = current;
                for (Display.Mode mode : display.getSupportedModes())
                    if (mode.getPhysicalWidth() == current.getPhysicalWidth() && mode.getPhysicalHeight() == current.getPhysicalHeight()
                            && mode.getRefreshRate() > best.getRefreshRate() + 0.5f)
                        best = mode;
                id = best.getModeId();
            }
            WindowManager.LayoutParams params = window.getAttributes();
            // Pages and players draw beside a punch hole or notch too, not
            // under a black bar (readers hide the status bar, and Android's
            // default then leaves the cutout's strip black; they pad their
            // controls by the safe area themselves).
            int cutout = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                       : Build.VERSION.SDK_INT >= 28 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES : -1;
            boolean changed = false;
            if (cutout >= 0 && params.layoutInDisplayCutoutMode != cutout) {
                params.layoutInDisplayCutoutMode = cutout;
                changed = true;
            }
            if (params.preferredDisplayModeId != id) {
                params.preferredDisplayModeId = id;
                changed = true;
            }
            if (changed) window.setAttributes(params);
            // Android 11 and later also go by what each surface asks for:
            // ColorOS and others hold a SurfaceView (Qt's) at 60 Hz unless it
            // votes for more, whatever the window prefers.
            rate = on ? display.getMode().getRefreshRate() : 0f;
            for (Display.Mode mode : display.getSupportedModes())
                if (on && mode.getModeId() == id) rate = mode.getRefreshRate();
            voteOnSurfaces(window.getDecorView());
        });
    }

    private static float rate = 0f;

    private static void vote(SurfaceHolder holder) {
        if (Build.VERSION.SDK_INT < 30 || holder == null || holder.getSurface() == null || !holder.getSurface().isValid()) return;
        try {
            holder.getSurface().setFrameRate(rate, android.view.Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
        } catch (Exception ignored) {
        }
    }

    // Every SurfaceView in the window, now and whenever its surface is
    // made again (the app coming back, a turn of the phone).
    private static final java.util.WeakHashMap<SurfaceView, Boolean> watched = new java.util.WeakHashMap<>();

    private static void voteOnSurfaces(View view) {
        if (view instanceof SurfaceView) {
            SurfaceView surface = (SurfaceView) view;
            vote(surface.getHolder());
            if (!watched.containsKey(surface)) {
                watched.put(surface, true);
                surface.getHolder().addCallback(new SurfaceHolder.Callback() {
                    @Override public void surfaceCreated(SurfaceHolder holder) { vote(holder); }
                    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { vote(holder); }
                    @Override public void surfaceDestroyed(SurfaceHolder holder) {}
                });
            }
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) voteOnSurfaces(group.getChildAt(i));
        }
    }

    // The window's brightness, 0 to 1, for the player's swipe; below 0 gives
    // it back to the system. Only this window: the phone's setting stays.
    public static void setBrightness(final Activity activity, final float level) {
        activity.runOnUiThread(() -> {
            WindowManager.LayoutParams params = activity.getWindow().getAttributes();
            params.screenBrightness = level < 0 ? WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE : Math.max(0.01f, Math.min(1f, level));
            activity.getWindow().setAttributes(params);
        });
    }

    // The brightness the window shows at: its own, else the system's.
    public static float brightness(Activity activity) {
        float own = activity.getWindow().getAttributes().screenBrightness;
        if (own >= 0) return own;
        try {
            return Settings.System.getInt(activity.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS) / 255f;
        } catch (Exception e) {
            return 0.5f;
        }
    }

    // Media volume, 0 to 1 (the phone's own, as its volume keys set it).
    public static float mediaVolume(Activity activity) {
        AudioManager audio = (AudioManager) activity.getSystemService(Context.AUDIO_SERVICE);
        int max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        return max > 0 ? audio.getStreamVolume(AudioManager.STREAM_MUSIC) / (float) max : 0;
    }

    public static void setMediaVolume(Activity activity, float level) {
        AudioManager audio = (AudioManager) activity.getSystemService(Context.AUDIO_SERVICE);
        int max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        audio.setStreamVolume(AudioManager.STREAM_MUSIC, Math.round(Math.max(0f, Math.min(1f, level)) * max), 0);
    }

    // Status and navigation bar icons for the screen under them: light icons
    // on SCANCORE's dark screens, dark icons on light ones, whatever the
    // phone's own light/dark mode is (src/qml/Main.qml, Theme.dark).
    public static void barIcons(final Activity activity, final boolean darkScreen) {
        activity.runOnUiThread(() -> {
            View decor = activity.getWindow().getDecorView();
            if (Build.VERSION.SDK_INT >= 30) {
                WindowInsetsController c = decor.getWindowInsetsController();
                if (c == null) return;
                int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                c.setSystemBarsAppearance(darkScreen ? 0 : mask, mask);
            } else if (Build.VERSION.SDK_INT >= 23) {
                int flags = decor.getSystemUiVisibility();
                int light = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                if (Build.VERSION.SDK_INT >= 26) light |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                decor.setSystemUiVisibility(darkScreen ? flags & ~light : flags | light);
            }
        });
    }

    // A television (Android TV, Google TV): the remote's D-pad drives the app.
    public static boolean isTv(Context context) {
        android.app.UiModeManager ui = (android.app.UiModeManager) context.getSystemService(Context.UI_MODE_SERVICE);
        return (ui != null && ui.getCurrentModeType() == android.content.res.Configuration.UI_MODE_TYPE_TELEVISION)
            || context.getPackageManager().hasSystemFeature("android.software.leanback");
    }

    // A TV lays SCANCORE out about 1280 wide, as a computer at 150 %: at
    // Android TV's own 960 dp everything came out twice the size it has
    // on a 1080p computer screen. The factor for QT_SCALE_FACTOR; 1 on
    // anything that is not a TV or is already that wide.
    public static float tvScale(Context context) {
        if (!isTv(context)) return 1f;
        android.util.DisplayMetrics m = context.getResources().getDisplayMetrics();
        if (m.density <= 0) return 1f;
        float wide = Math.max(m.widthPixels, m.heightPixels) / m.density;
        return wide > 0 && wide < 1280f ? wide / 1280f : 1f;
    }

    // Readers on full screen (as Mihon's): the status and navigation bars
    // hidden while the reader's own controls are, a swipe from the edge
    // shows them for a moment; shown again with the controls.
    public static void systemBars(final Activity activity, final boolean show) {
        activity.runOnUiThread(() -> {
            View decor = activity.getWindow().getDecorView();
            if (Build.VERSION.SDK_INT >= 30) {
                WindowInsetsController c = decor.getWindowInsetsController();
                if (c == null) return;
                if (show) {
                    c.show(android.view.WindowInsets.Type.systemBars());
                } else {
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    c.hide(android.view.WindowInsets.Type.systemBars());
                }
            } else {
                int immersive = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
                int flags = decor.getSystemUiVisibility();
                decor.setSystemUiVisibility(show ? flags & ~immersive : flags | immersive);
            }
        });
    }

    // "landscape": landscape whichever way the phone is held, even with
    // auto-rotate off. "portrait": portrait until the phone is held upright,
    // then free again, so turning it later still goes full screen. Anything
    // else: free (the system's rotation setting decides).
    public static void orient(final Activity activity, final String mode) {
        activity.runOnUiThread(() -> {
            if (release != null) {
                release.disable();
                release = null;
            }
            if ("landscape".equals(mode)) {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
                return;
            }
            if (!"portrait".equals(mode)) {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                return;
            }
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
            OrientationEventListener listener = new OrientationEventListener(activity) {
                @Override
                public void onOrientationChanged(int degrees) {
                    if (degrees == ORIENTATION_UNKNOWN || (degrees > 20 && degrees < 340)) return;
                    disable();
                    if (release == this) release = null;
                    activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                }
            };
            if (listener.canDetectOrientation()) {
                release = listener;
                listener.enable();
            } else {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
            }
        });
    }
}
