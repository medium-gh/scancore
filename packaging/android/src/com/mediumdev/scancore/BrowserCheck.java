package com.mediumdev.scancore;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.net.Uri;
import android.os.Message;
import android.webkit.CookieManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import java.io.ByteArrayInputStream;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

// A site's browser check on a phone (src/app/Platform.cpp). The site opens
// in Android's web view with SCANCORE's user agent; when the reader presses
// Done (or Back) the cookies the site set go to SCANCORE and to extensions,
// so their requests pass the check too.
public final class BrowserCheck extends Activity {
    private WebView web;
    private String url;
    private boolean reported;
    // A sign-in that hands back a token in its address (AniList): the
    // address to report instead of the one it started at.
    private String handedBack;

    static native void nativeDone(String url, String cookies);

    public static void start(Context context, String url, String userAgent, String title, String done) {
        Intent intent = new Intent(context, BrowserCheck.class)
            .putExtra("url", url)
            .putExtra("userAgent", userAgent)
            .putExtra("title", title)
            .putExtra("done", done);
        if (!(context instanceof Activity)) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
    }

    private int dp(float value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics()));
    }

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Intent intent = getIntent();
        url = intent.getStringExtra("url");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(10, 10, 10));
        root.setFitsSystemWindows(true);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(16), dp(8), dp(8), dp(8));
        bar.setBackgroundColor(Color.rgb(20, 20, 20));
        TextView title = new TextView(this);
        title.setText(intent.getStringExtra("title"));
        title.setTextColor(Color.rgb(230, 230, 230));
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        title.setMaxLines(3);
        bar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Button done = new Button(this);
        done.setText(intent.getStringExtra("done"));
        done.setAllCaps(false);
        done.setOnClickListener(v -> finish());
        bar.addView(done, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        web = new WebView(this);
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        String userAgent = intent.getStringExtra("userAgent");
        if (userAgent != null && !userAgent.isEmpty()) settings.setUserAgentString(userAgent);
        CookieManager cookies = CookieManager.getInstance();
        cookies.setAcceptCookie(true);
        cookies.setAcceptThirdPartyCookies(web, true);
        // Links and the check's own redirects stay in this view. Once the
        // site sets a new Cloudflare clearance the check is passed, and the
        // screen closes by itself.
        final String before = clearance(cookies, url);
        // A site's check (not a sign-in): the view stays on that site. Ads
        // that send the page elsewhere on a tap, open pop-ups or pop-unders
        // are refused, and requests to ad networks never leave.
        final boolean lockSite = userAgent != null && !userAgent.isEmpty();
        final String home = site(Uri.parse(url).getHost());
        settings.setSupportMultipleWindows(lockSite);
        settings.setJavaScriptCanOpenWindowsAutomatically(!lockSite);
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onCreateWindow(WebView view, boolean dialog, boolean userGesture, Message result) {
                return false;
            }
        });
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                if (!lockSite || !request.isForMainFrame()) return false;
                Uri target = request.getUrl();
                String scheme = target.getScheme();
                if (scheme == null || !(scheme.equals("http") || scheme.equals("https"))) return true;
                return !site(target.getHost()).equals(home) && !CHECK_HOSTS.contains(site(target.getHost()));
            }

            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                if (lockSite && isAd(request.getUrl().getHost()))
                    return new WebResourceResponse("text/plain", "utf-8", 204, "No Content", null, new ByteArrayInputStream(new byte[0]));
                return null;
            }
        });
        // Looked for four times a second: a site's page behind the check
        // may never quite finish loading (ads, covers), and waiting for it
        // kept the screen open long after the check had passed.
        final android.os.Handler main = new android.os.Handler(android.os.Looper.getMainLooper());
        main.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isFinishing() || isDestroyed()) return;
                String now = clearance(cookies, url);
                String address = web.getUrl();
                if (address != null && address.contains("access_token=")) {
                    handedBack = address;
                    finish();
                } else if (now != null && !now.equals(before)) finish();
                else main.postDelayed(this, 250);
            }
        }, 250);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        root.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        if (state == null) web.loadUrl(url);
        else web.restoreState(state);
    }

    // Browser checks that may move the page to their own site.
    private static final java.util.Set<String> CHECK_HOSTS = new java.util.HashSet<>(java.util.Arrays.asList(
        "cloudflare.com", "ddos-guard.net", "hcaptcha.com", "recaptcha.net"));

    // Ad and pop-under networks scan sites use (the same list as
    // src/app/WebBridge.cpp).
    private static final String[] AD_HOSTS = {
        "doubleclick.net", "googlesyndication.com", "googleadservices.com", "adservice.google.com",
        "popads.net", "popcash.net", "propellerads.com", "propellerclick.com", "adsterra.com", "adsterratech.com",
        "highperformanceformat.com", "profitablecpmrate.com", "profitablegatecpm.com", "effectiveratecpm.com",
        "exoclick.com", "exosrv.com", "juicyads.com", "trafficjunky.net", "hilltopads.net", "clickadu.com",
        "adcash.com", "monetag.com", "onclickads.net", "onclickalgo.com", "a-ads.com", "adskeeper.com",
        "mgid.com", "revcontent.com", "taboola.com", "outbrain.com", "pubfuture.com", "galaksion.com",
        "admaven.com", "ad-maven.com", "adtelligent.com", "bidgear.com", "richads.com", "evadav.com",
        "pushwhy.com", "rollerads.com", "dtscout.com", "dtscdn.com"};

    static boolean isAd(String host) {
        if (host == null) return false;
        String h = host.toLowerCase();
        for (String ad : AD_HOSTS)
            if (h.equals(ad) || h.endsWith("." + ad)) return true;
        return false;
    }

    // The name a site is registered under: "manga.example.co.uk" ->
    // "example.co.uk", "www.example.com" -> "example.com".
    static String site(String host) {
        if (host == null) return "";
        String[] labels = host.toLowerCase().split("\\.");
        int n = labels.length;
        if (n <= 2) return host.toLowerCase();
        String second = labels[n - 2];
        boolean twoPart = labels[n - 1].length() == 2
            && java.util.Arrays.asList("co", "com", "net", "org", "gov", "ac", "edu", "ne", "or").contains(second);
        int keep = twoPart ? 3 : 2;
        StringBuilder out = new StringBuilder();
        for (int i = n - keep; i < n; i++) out.append(i > n - keep ? "." : "").append(labels[i]);
        return out.toString();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        if (web != null) web.saveState(state);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (web != null && web.canGoBack()) web.goBack();
        else finish();
    }

    private static String clearance(CookieManager cookies, String url) {
        String header = url == null ? null : cookies.getCookie(url);
        if (header == null) return null;
        for (String part : header.split(";")) {
            String p = part.trim();
            if (p.startsWith("cf_clearance=")) return p.substring("cf_clearance=".length());
        }
        return null;
    }

    private void report() {
        if (reported || url == null) return;
        reported = true;
        CookieManager cookies = CookieManager.getInstance();
        cookies.flush();
        String header = cookies.getCookie(url);
        nativeDone(handedBack != null ? handedBack : url, header == null ? "" : header);
    }

    @Override
    public void finish() {
        report();
        super.finish();
    }

    @Override
    protected void onDestroy() {
        // Leaving any other way (the system closing it) still hands over
        // what the site set.
        if (isFinishing()) report();
        if (web != null) {
            web.stopLoading();
            web.destroy();
            web = null;
        }
        super.onDestroy();
    }
}
