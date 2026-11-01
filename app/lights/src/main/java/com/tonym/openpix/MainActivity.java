package com.tonym.openpix;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.CRC32;

/**
 * "Lights" app for OpenPix controllers. The UI is a bundled web page (assets/index.html);
 * this activity gives it native networking (no browser CORS limits), Wi-Fi discovery,
 * saved settings and GitHub firmware downloads.
 */
public class MainActivity extends Activity {
    private WebView web;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService pool = Executors.newFixedThreadPool(4);
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("openpix", Context.MODE_PRIVATE);
        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        web.setBackgroundColor(0xFF0F1216);
        web.setWebViewClient(new WebViewClient());
        web.addJavascriptInterface(new Bridge(), "Native");
        setContentView(web);
        web.loadUrl("file:///android_asset/index.html");
    }

    @Override
    public void onBackPressed() {
        if (web.canGoBack()) web.goBack(); else super.onBackPressed();
    }

    private void js(String code) {
        ui.post(() -> web.evaluateJavascript(code, null));
    }

    private static String q(String s) { return JSONObject.quote(s == null ? "" : s); }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
        return o.toByteArray();
    }

    /** Simple HTTP request. Returns {status, body}. */
    private static Object[] request(String method, String url, byte[] body, String ctype, int timeout) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(timeout);
        c.setReadTimeout(Math.max(timeout, 15000));
        c.setRequestMethod(method);
        c.setUseCaches(false);
        if (body != null) {
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", ctype != null ? ctype : "application/x-www-form-urlencoded");
            c.setFixedLengthStreamingMode(body.length);
            try (OutputStream o = c.getOutputStream()) { o.write(body); }
        }
        int code = c.getResponseCode();
        InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
        String text = in == null ? "" : new String(readAll(in), StandardCharsets.UTF_8);
        c.disconnect();
        return new Object[]{code, text};
    }

    private class Bridge {
        @JavascriptInterface
        public void http(String id, String method, String url, String body, int timeout) {
            pool.execute(() -> {
                try {
                    Object[] r = request(method, url, body == null || body.isEmpty() ? null : body.getBytes(StandardCharsets.UTF_8),
                            null, timeout > 0 ? timeout : 4000);
                    js("__cb(" + q(id) + "," + r[0] + "," + q((String) r[1]) + ")");
                } catch (Exception e) {
                    js("__cb(" + q(id) + ",0," + q(e.getMessage()) + ")");
                }
            });
        }

        @JavascriptInterface
        public String getPref(String k) { return prefs.getString(k, ""); }

        @JavascriptInterface
        public void setPref(String k, String v) { prefs.edit().putString(k, v).apply(); }

        @JavascriptInterface
        public void openUrl(String url) {
            ui.post(() -> startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))));
        }

        @JavascriptInterface
        public String localIp() {
            try {
                for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                    if (!ni.isUp() || ni.isLoopback()) continue;
                    for (InetAddress a : Collections.list(ni.getInetAddresses()))
                        if (a instanceof Inet4Address && a.isSiteLocalAddress()) return a.getHostAddress();
                }
            } catch (Exception ignored) { }
            return "";
        }

        /** Probe every address on the phone's /24 for an OpenPix controller. */
        @JavascriptInterface
        public void scan(String id) {
            new Thread(() -> {
                String ip = localIp();
                if (ip.isEmpty()) { js("__cb(" + q(id) + ",0,'no wifi')"); return; }
                String base = ip.substring(0, ip.lastIndexOf('.') + 1);
                ExecutorService ex = Executors.newFixedThreadPool(48);
                AtomicInteger found = new AtomicInteger();
                for (int i = 1; i < 255; i++) {
                    final String host = base + i;
                    ex.execute(() -> {
                        try {
                            Object[] r = request("GET", "http://" + host + "/api/status", null, null, 700);
                            String t = (String) r[1];
                            if ((int) r[0] == 200 && t.contains("\"fw\":\"OpenPix")) {
                                found.incrementAndGet();
                                js("__found(" + q(host) + "," + q(t) + ")");
                            }
                        } catch (Exception ignored) { }
                    });
                }
                ex.shutdown();
                try { ex.awaitTermination(30, TimeUnit.SECONDS); } catch (InterruptedException ignored) { }
                js("__cb(" + q(id) + ",200," + q(String.valueOf(found.get())) + ")");
            }).start();
        }

        /** Download a firmware file, check size + CRC32, then upload it to the controller. */
        @JavascriptInterface
        public void installFirmware(String id, String ctrl, String binUrl, long size, long crc) {
            new Thread(() -> {
                try {
                    js("__prog(" + q(id) + ",'Downloading from GitHub…',0)");
                    HttpURLConnection d = (HttpURLConnection) new URL(binUrl).openConnection();
                    d.setUseCaches(false);
                    d.setConnectTimeout(10000);
                    d.setReadTimeout(30000);
                    byte[] bin = readAll(d.getInputStream());
                    d.disconnect();
                    if (size > 0 && bin.length != size) throw new Exception("download size mismatch");
                    CRC32 c = new CRC32();
                    c.update(bin);
                    if (crc > 0 && c.getValue() != crc) throw new Exception("download checksum mismatch");

                    js("__prog(" + q(id) + ",'Erasing and uploading to controller…',0)");
                    HttpURLConnection u = (HttpURLConnection) new URL("http://" + ctrl + "/api/update").openConnection();
                    u.setConnectTimeout(5000);
                    u.setReadTimeout(60000);
                    u.setRequestMethod("POST");
                    u.setDoOutput(true);
                    u.setRequestProperty("Content-Type", "application/octet-stream");
                    u.setFixedLengthStreamingMode(bin.length);
                    try (OutputStream o = u.getOutputStream()) {
                        int step = 4096, last = -1;
                        for (int i = 0; i < bin.length; i += step) {
                            o.write(bin, i, Math.min(step, bin.length - i));
                            int pct = (int) ((i + step) * 100L / bin.length);
                            if (pct != last) { last = pct; js("__prog(" + q(id) + ",'Uploading…'," + Math.min(pct, 100) + ")"); }
                        }
                    }
                    int code = u.getResponseCode();
                    InputStream in = code >= 400 ? u.getErrorStream() : u.getInputStream();
                    String text = in == null ? "" : new String(readAll(in), StandardCharsets.UTF_8);
                    u.disconnect();
                    js("__cb(" + q(id) + "," + code + "," + q(text) + ")");
                } catch (Exception e) {
                    js("__cb(" + q(id) + ",0," + q("Update failed: " + e.getMessage() + ". Nothing was changed.") + ")");
                }
            }).start();
        }
    }
}
