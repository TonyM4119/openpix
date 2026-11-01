package com.tonym.openpix;

import android.app.Activity;
import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
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
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
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
    private File uiFile;
    private int loadedUi;
    static final String UI_URL = "https://raw.githubusercontent.com/TonyM4119/openpix/main/app/lights/src/main/assets/index.html";
    static final String MARK = "<!--openpix-ui-->";

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
        uiFile = new File(getFilesDir(), "ui.html");
        loadUi();
    }

    private static int uiVer(String html) {
        Matcher m = Pattern.compile("name=\"ui-version\" content=\"(\\d+)\"").matcher(html == null ? "" : html);
        return m.find() ? Integer.parseInt(m.group(1)) : 0;
    }

    private String readFile(File f) {
        try (java.io.FileInputStream in = new java.io.FileInputStream(f)) {
            return new String(readAll(in), StandardCharsets.UTF_8);
        } catch (Exception e) { return null; }
    }

    /** Use the newest UI: the one downloaded from GitHub if newer than the bundled one. */
    private void loadUi() {
        String html;
        try (InputStream in = getAssets().open("index.html")) {
            html = new String(readAll(in), StandardCharsets.UTF_8);
        } catch (Exception e) { html = "<p>UI missing</p>"; }
        String cached = uiFile.exists() ? readFile(uiFile) : null;
        if (cached != null && cached.contains(MARK) && uiVer(cached) > uiVer(html)) html = cached;
        loadedUi = uiVer(html);
        web.loadDataWithBaseURL("file:///android_asset/", html, "text/html", "utf-8", null);
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
        public int apiLevel() { return 2; }

        @JavascriptInterface
        public int uiVersion() { return loadedUi; }

        @JavascriptInterface
        public int appVersion() {
            try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionCode; }
            catch (Exception e) { return 0; }
        }

        /** Download the latest UI page from GitHub; reload if it is newer. */
        @JavascriptInterface
        public void checkUi(String id) {
            pool.execute(() -> {
                try {
                    Object[] r = request("GET", UI_URL + "?t=" + System.currentTimeMillis(), null, null, 8000);
                    String html = (String) r[1];
                    int v = uiVer(html);
                    if ((int) r[0] == 200 && html.contains(MARK) && v > loadedUi) {
                        try (FileOutputStream o = new FileOutputStream(uiFile)) { o.write(html.getBytes(StandardCharsets.UTF_8)); }
                        js("__cb(" + q(id) + ",200,'updated')");
                        ui.postDelayed(MainActivity.this::loadUi, 1200);
                    } else js("__cb(" + q(id) + ",200,'same')");
                } catch (Exception e) { js("__cb(" + q(id) + ",0," + q(e.getMessage()) + ")"); }
            });
        }

        /** Download a new APK and hand it to Android's installer (user confirms). */
        @JavascriptInterface
        public String installApk(String url) {
            if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
                ui.post(() -> startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName()))));
                return "permission";
            }
            DownloadManager dm = (DownloadManager) getSystemService(DOWNLOAD_SERVICE);
            File f = new File(getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "Lights.apk");
            if (f.exists()) f.delete();
            DownloadManager.Request rq = new DownloadManager.Request(Uri.parse(url))
                    .setTitle("Lights update")
                    .setMimeType("application/vnd.android.package-archive")
                    .setDestinationInExternalFilesDir(MainActivity.this, Environment.DIRECTORY_DOWNLOADS, "Lights.apk");
            final long did = dm.enqueue(rq);
            BroadcastReceiver rcv = new BroadcastReceiver() {
                @Override public void onReceive(Context c, Intent i) {
                    if (i.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1) != did) return;
                    try { unregisterReceiver(this); } catch (Exception ignored) { }
                    Uri u = dm.getUriForDownloadedFile(did);
                    if (u == null) { js("toast('Download failed')"); return; }
                    Intent inst = new Intent(Intent.ACTION_VIEW);
                    inst.setDataAndType(u, "application/vnd.android.package-archive");
                    inst.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(inst);
                }
            };
            IntentFilter filt = new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE);
            if (Build.VERSION.SDK_INT >= 33) registerReceiver(rcv, filt, Context.RECEIVER_EXPORTED);
            else registerReceiver(rcv, filt);
            return "started";
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
