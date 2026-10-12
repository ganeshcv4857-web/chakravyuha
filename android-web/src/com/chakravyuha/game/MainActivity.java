package com.chakravyuha.game;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.ValueCallback;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * Chakravyuha for Android: the web build of the game in a full-screen
 * WebView. The game files are packed in the app (assets/web) and served
 * from a private https address, so solo and same-device battles work
 * offline; online battles connect to the game server over the internet.
 */
public class MainActivity extends Activity {
    private static final String HOST = "appassets.chakravyuha";
    private WebView web;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        web.setBackgroundColor(0xFF18110B);
        web.setWebViewClient(new AssetClient());
        setContentView(web);
        hideSystemBars();

        if (state == null)
            web.loadUrl("https://" + HOST + "/index.html");
        else
            web.restoreState(state);
    }

    /** Serves https://appassets.chakravyuha/... from the app's assets/web folder. */
    private class AssetClient extends WebViewClient {
        private final Map<String, String> types = new HashMap<>();

        AssetClient() {
            types.put("html", "text/html");
            types.put("js", "application/javascript");
            types.put("wasm", "application/wasm");
            types.put("data", "application/octet-stream");
            types.put("json", "application/json");
            types.put("webmanifest", "application/manifest+json");
            types.put("png", "image/png");
            types.put("txt", "text/plain");
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest req) {
            if (!HOST.equals(req.getUrl().getHost()))
                return null; // anything else (the game server) goes to the network
            String path = req.getUrl().getPath();
            if (path == null || path.equals("/"))
                path = "/index.html";
            String ext = path.substring(path.lastIndexOf('.') + 1);
            String mime = types.containsKey(ext) ? types.get(ext) : "application/octet-stream";
            try {
                InputStream in = getAssets().open("web" + path);
                return new WebResourceResponse(mime, mime.startsWith("text") ? "utf-8" : null, in);
            } catch (IOException e) {
                return new WebResourceResponse("text/plain", "utf-8", 404, "Not Found", null, null);
            }
        }
    }

    /** Back goes back a screen in the game; on the title screen it closes the app. */
    @Override
    public void onBackPressed() {
        web.evaluateJavascript("window.chakraAtRoot ? 'exit' : (window.chakraBack = 1, 'back')",
                new ValueCallback<String>() {
                    @Override
                    public void onReceiveValue(String result) {
                        if (result != null && result.contains("exit"))
                            finish();
                    }
                });
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus)
            hideSystemBars();
    }

    // Pausing the WebView stops the game's timers and sound while in the background.
    @Override
    protected void onPause() {
        web.onPause();
        web.pauseTimers();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        web.resumeTimers();
        web.onResume();
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        web.saveState(out);
    }

    @Override
    protected void onDestroy() {
        web.destroy();
        super.onDestroy();
    }
}
