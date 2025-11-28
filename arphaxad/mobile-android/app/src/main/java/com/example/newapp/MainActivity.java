package com.example.newapp;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.preference.PreferenceManager;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;

import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.view.WindowManager;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Timer;
import java.util.TimerTask;

public class MainActivity extends AppCompatActivity {
    private TextView textJson;
    private TextView textError;

    private Timer timer;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    // Format timestamp like: 2025-04-05 14:32:10.250
    private final SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault());

    private final long REQUEST_INTERVAL = 1000; // 1 second
    private boolean isRequestInProgress = false;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);

        // prevents the screen from turning off while the app is in the foreground
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Find views by ID
        textJson = findViewById(R.id.textJson);
        textError = findViewById(R.id.textError);

        runOnUiThread(() -> textError.setText(""));

        startHttpPolling();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    @Override
    public void onResume() {
        super.onResume();
        // Re-apply the flag when app comes back to foreground
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    public void onPause() {
        super.onPause();
        // Optional: Clear the flag when app goes to background (saves battery)
        // Remove this if you want screen to stay on even in background (not recommended)
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private void scheduleNext() {
        mainHandler.postDelayed(this::poll, REQUEST_INTERVAL);
    }

    private void startHttpPolling() {
        poll();
    }

    private void poll() {
        if (isRequestInProgress) {
            // Skip if already waiting or requesting
            scheduleNext();
            return;
        }

        isRequestInProgress = true;
        fetchJsonFromEsp();
    }

    public void onRequestComplete() {
        isRequestInProgress = false;
        scheduleNext();
    }

    private void fetchJsonFromEsp() {
        new Thread(() -> {
            HttpURLConnection conn = null;
            BufferedReader reader = null;
            try {
                URL url = new URL("http://192.168.4.1/gps");
                conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(250);  // 250ms
                conn.setReadTimeout(250);
                conn.setRequestProperty("Connection", "close");

                int responseCode = conn.getResponseCode();
                if (responseCode == HttpURLConnection.HTTP_OK) {
                    reader = new BufferedReader(new InputStreamReader(conn.getInputStream()));
                    StringBuilder sb = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        sb.append(line);
                    }
                    String rawJson = sb.toString().trim();

                    // Pretty-print JSON (manual, no Gson)
                    String prettyJson = formatJson(rawJson);

                    // Add timestamp
                    String timestamp = sdf.format(new Date());
                    String logLine = timestamp + " | " + prettyJson;

                    runOnUiThread(() -> textJson.setText(logLine));
                } else {
                    //appendToFile("HTTP " + responseCode + " at " + sdf.format(new Date()));
                    runOnUiThread(() -> textError.setText("HTTP " + responseCode + " at " + sdf.format(new Date())));
                }
            } catch (Exception e) {
                String timestamp = sdf.format(new Date());
                //appendToFile(timestamp + " | ERROR: " + e.getMessage());
                runOnUiThread(() -> textError.setText("result: " + timestamp + " | ERROR: " + e.getMessage()));
            } finally {
                if (reader != null) {
                    try { reader.close(); } catch (IOException ignored) {}
                }
                if (conn != null) {
                    conn.disconnect();
                }
            }
            onRequestComplete();
        }).start();
    }

    // Simple JSON pretty printer (no external libs)
    private String formatJson(String json) {
        try {
            json = json.trim();
            if (json.startsWith("{")) {
                JSONObject jsonObject = new JSONObject(json);
                return jsonObject.toString(2); // indent by 2 spaces
            } else if (json.startsWith("[")) {
                // If it's a JSON array (rare in this case), still format
                return json.replace(",", ",\n  ").replace("{", "{\n  ").replace("}", "\n}");
            }
        } catch (Exception ignored) {}
        return json; // fallback: return raw
    }

    private void stopHttpPolling() {
        mainHandler.removeCallbacksAndMessages(null);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopHttpPolling();
        if (timer != null) {
            timer.cancel();
            timer = null;
        }
    }
}
