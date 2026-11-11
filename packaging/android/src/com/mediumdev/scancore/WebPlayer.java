package com.mediumdev.scancore;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.os.Message;
import android.webkit.GeolocationPermissions;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import androidx.webkit.JavaScriptReplyProxy;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import java.util.Collections;

// The anime player's web view (src/qml/components/WebPlayerSystem.qml), which
// Qt WebView creates with two of Android's defaults that do not suit a player:
// - a page may only start video by itself muted, so every episode started
//   without sound;
// - pages are told full screen is not supported unless the web view's client
//   shows the page's full screen view. That view now covers the window, in
//   landscape and without the system bars, until the player leaves full
//   screen or Back is pressed;
// - a pop-up the player's ads open (on the first tap) loads in the web view
//   itself, so an ad site replaced the player. Pop-ups are now refused, and
//   the page cannot leave the player wrapper (WebPlayerSystem.qml);
// - Back closed the whole player while the player's own menu (quality,
//   subtitles) was open. A script in the player's frame now says when that
//   menu opens, and Back closes it first (closeMenu).
public final class WebPlayer {
    private WebPlayer() {}

    // Runs in every frame of the player page. The built-in player is
    // ArtPlayer, which marks its open settings and context menus with a class
    // on the player and closes them on a click outside it.
    private static final String MENU_SCRIPT = "(function(){"
        + "if(!window.scBack||window.__scMenu)return;window.__scMenu=1;"
        + "var p=null,open=false;"
        + "function find(){if(!p||!p.isConnected)p=document.querySelector('.art-video-player');return p}"
        + "function shown(){var q=find();return !!q&&(q.classList.contains('art-setting-show')||q.classList.contains('art-contextmenu-show'))}"
        + "function report(){var o=shown();if(o!==open){open=o;scBack.postMessage(o?'open':'closed')}}"
        + "scBack.onmessage=function(e){if(e.data!=='close'||!find())return;"
        + "document.documentElement.dispatchEvent(new MouseEvent('click',{bubbles:true}));"
        + "p.classList.remove('art-setting-show','art-contextmenu-show');report()};"
        + "new MutationObserver(report).observe(document,{subtree:true,attributes:true,attributeFilter:['class']});"
        + "})()";

    // Whether a frame said its menu is open, and that frame. Written on the
    // UI thread; Back asks from Qt's thread.
    private static volatile boolean menuOpen;
    private static JavaScriptReplyProxy menuFrame;

    // Back while the player's menu is open: the menu closes, and true.
    public static boolean closeMenu(final Activity activity) {
        if (!menuOpen) return false;
        menuOpen = false;
        activity.runOnUiThread(WebPlayer::sendClose);
        return true;
    }

    private static void sendClose() {
        JavaScriptReplyProxy frame = menuFrame;
        menuFrame = null;
        menuOpen = false;
        if (frame != null) frame.postMessage("close");
    }

    private static void watchMenus(WebView web) {
        if (!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)
            || !WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) return;
        // The player's host comes from the sources manifest, so any frame.
        WebViewCompat.addWebMessageListener(web, "scBack", Collections.singleton("*"),
            (view, message, origin, mainFrame, reply) -> {
                if ("open".equals(message.getData())) {
                    menuFrame = reply;
                    menuOpen = true;
                } else if (reply == menuFrame) {
                    menuFrame = null;
                    menuOpen = false;
                }
            });
        WebViewCompat.addDocumentStartJavaScript(web, MENU_SCRIPT, Collections.singleton("*"));
    }

    // The player page may load: its web view is prepared, or none turned up.
    static native void nativePrepared();

    // Prepares the web view a player page has just created. Qt adds it to the
    // window a moment later, so this looks again for a few seconds. The page
    // waits for nativePrepared(): a player that starts before this finds no
    // sound and no full screen, and does not look again.
    public static void prepare(final Activity activity) {
        activity.runOnUiThread(() -> {
            menuFrame = null;
            menuOpen = false;
            attempt(activity, 0);
        });
    }

    // Every 100 ms; after 5 s the page loads anyway, and the search goes on
    // for half a minute in case the web view still turns up.
    private static void attempt(final Activity activity, final int tries) {
        if (activity.isFinishing() || activity.isDestroyed()) return;
        if (prepareIn(activity, activity.getWindow().getDecorView()) > 0) {
            if (tries <= 50) nativePrepared();
            return;
        }
        if (tries == 50) nativePrepared();
        if (tries < 300)
            new Handler(Looper.getMainLooper()).postDelayed(() -> attempt(activity, tries + 1), 100);
    }

    // How many web views were prepared now (not before).
    private static int prepareIn(Activity activity, View view) {
        if (view instanceof WebView) {
            WebView web = (WebView) view;
            WebChromeClient client = web.getWebChromeClient();
            if (client instanceof Client) return 0;
            web.getSettings().setMediaPlaybackRequiresUserGesture(false);
            // With several windows allowed, window.open asks onCreateWindow
            // (refused) instead of loading the pop-up here.
            web.getSettings().setSupportMultipleWindows(true);
            web.getSettings().setJavaScriptCanOpenWindowsAutomatically(false);
            web.setWebChromeClient(new Client(activity, web, client));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) web.setWebViewClient(new Guard(web.getWebViewClient()));
            watchMenus(web);
            // A TV: the remote's keys stay with the app, which works the
            // player through its message API (WatchPage.qml). A focused web
            // view would take every key and the D-pad would move in the page.
            if (Screen.isTv(activity)) {
                web.setFocusable(false);
                web.setFocusableInTouchMode(false);
            }
            return 1;
        }
        int prepared = 0;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++)
                prepared += prepareIn(activity, group.getChildAt(i));
        }
        return prepared;
    }

    // Qt's own page client (load progress and errors reach QML through it),
    // with the page kept on the player wrapper: the wrapper is loaded as
    // HTML on https://reanime.to/ (WebPlayerSystem.qml), so a link or script
    // that sends the whole page elsewhere is an ad and goes nowhere. Frames
    // inside it (the player) are not affected.
    private static final class Guard extends WebViewClient {
        private final WebViewClient qt;

        Guard(WebViewClient qt) {
            this.qt = qt;
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            if (request.isForMainFrame()) {
                String url = request.getUrl().toString();
                if (!url.startsWith("https://reanime.to/") && !url.startsWith("about:") && !url.startsWith("data:")) return true;
            }
            return qt != null && qt.shouldOverrideUrlLoading(view, request);
        }

        @Override
        public void onPageStarted(WebView view, String url, Bitmap favicon) {
            if (qt != null) qt.onPageStarted(view, url, favicon);
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            if (qt != null) qt.onPageFinished(view, url);
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            if (qt != null) qt.onReceivedError(view, request, error);
        }

        @Override
        public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
            if (qt != null) qt.onReceivedHttpError(view, request, response);
        }

        @Override
        public void onLoadResource(WebView view, String url) {
            if (qt != null) qt.onLoadResource(view, url);
        }
    }

    // Qt's own client (title, progress and icon reach QML through it) with
    // full screen added.
    private static final class Client extends WebChromeClient {
        private final Activity activity;
        private final WebView web;
        private final WebChromeClient qt;
        private FrameLayout cover;
        private CustomViewCallback callback;
        private int orientation;
        private boolean barsShown;
        // The player page closed while in full screen.
        private final View.OnAttachStateChangeListener closed = new View.OnAttachStateChangeListener() {
            @Override
            public void onViewAttachedToWindow(View view) {}

            @Override
            public void onViewDetachedFromWindow(View view) {
                CustomViewCallback pending = callback;
                onHideCustomView();
                if (pending != null) pending.onCustomViewHidden();
            }
        };

        Client(Activity activity, WebView web, WebChromeClient qt) {
            this.activity = activity;
            this.web = web;
            this.qt = qt;
        }

        @Override
        public void onProgressChanged(WebView view, int progress) {
            if (qt != null) qt.onProgressChanged(view, progress);
        }

        @Override
        public void onReceivedIcon(WebView view, Bitmap icon) {
            if (qt != null) qt.onReceivedIcon(view, icon);
        }

        @Override
        public void onReceivedTitle(WebView view, String title) {
            if (qt != null) qt.onReceivedTitle(view, title);
        }

        // Ads' pop-ups: never.
        @Override
        public boolean onCreateWindow(WebView view, boolean isDialog, boolean isUserGesture, Message resultMsg) {
            return false;
        }

        @Override
        public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
            if (qt != null) qt.onGeolocationPermissionsShowPrompt(origin, callback);
            else callback.invoke(origin, false, false);
        }

        @Override
        public void onShowCustomView(View view, CustomViewCallback callback) {
            if (cover != null) {
                callback.onCustomViewHidden();
                return;
            }
            this.callback = callback;
            cover = new FrameLayout(activity) {
                @Override
                public boolean dispatchKeyEvent(KeyEvent event) {
                    if (event.getKeyCode() != KeyEvent.KEYCODE_BACK) return super.dispatchKeyEvent(event);
                    if (event.getAction() != KeyEvent.ACTION_UP) return true;
                    // The player's menu closes first, then full screen.
                    if (menuOpen) {
                        sendClose();
                        return true;
                    }
                    CustomViewCallback leave = Client.this.callback;
                    if (leave != null) leave.onCustomViewHidden();
                    return true;
                }
            };
            cover.setBackgroundColor(Color.BLACK);
            cover.addView(view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            decor().addView(cover, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            view.setFocusable(true);
            view.setFocusableInTouchMode(true);
            view.requestFocus();
            orientation = activity.getRequestedOrientation();
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            barsShown = showBars(false);
            web.addOnAttachStateChangeListener(closed);
        }

        @Override
        public void onHideCustomView() {
            if (cover == null) return;
            web.removeOnAttachStateChangeListener(closed);
            decor().removeView(cover);
            cover.removeAllViews();
            cover = null;
            callback = null;
            activity.setRequestedOrientation(orientation);
            if (barsShown) showBars(true);
        }

        private ViewGroup decor() {
            return (ViewGroup) activity.getWindow().getDecorView();
        }

        // Shows or hides the status and navigation bars; answers whether they
        // were showing before.
        @SuppressWarnings("deprecation")
        private boolean showBars(boolean show) {
            View decor = activity.getWindow().getDecorView();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                WindowInsets insets = decor.getRootWindowInsets();
                boolean shown = insets == null || insets.isVisible(WindowInsets.Type.statusBars());
                WindowInsetsController bars = activity.getWindow().getInsetsController();
                if (bars != null) {
                    if (show) {
                        bars.show(WindowInsets.Type.systemBars());
                    } else {
                        bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                        bars.hide(WindowInsets.Type.systemBars());
                    }
                }
                return shown;
            }
            int flags = decor.getSystemUiVisibility();
            int hidden = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            decor.setSystemUiVisibility(show ? flags & ~hidden : flags | hidden);
            return (flags & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0;
        }
    }
}
