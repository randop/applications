package com.example.raylibcpp;

import android.app.NativeActivity;
import android.content.Context;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.view.WindowManager;

public class NativeLoader extends NativeActivity {

    static {
        System.loadLibrary("main");
    }

    // Native methods that will be implemented in C++
    public native void nativePushChar(int codepoint);
    public native void nativePushKey(int key);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(savedInstanceState);
    }

    public void showSoftInput() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager imm =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                View view = getWindow().getDecorView();
                view.requestFocus();
                imm.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
            }
        });
    }

    public void hideSoftInput() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager imm =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                View view = getWindow().getDecorView();
                imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
            }
        });
    }

    // ---------- Soft keyboard → Raylib ----------
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            int keyCode = event.getKeyCode();
            int unicode = event.getUnicodeChar();

            // Printable character
            if (unicode > 0) {
                nativePushChar(unicode);
            }

            // Special keys
            switch (keyCode) {
                case KeyEvent.KEYCODE_DEL:          // Backspace
                    nativePushKey(259);             // KEY_BACKSPACE in raylib
                    break;
                case KeyEvent.KEYCODE_ENTER:
                case KeyEvent.KEYCODE_NUMPAD_ENTER:
                    nativePushKey(257);             // KEY_ENTER
                    break;
                case KeyEvent.KEYCODE_DPAD_LEFT:
                    nativePushKey(263);             // KEY_LEFT
                    break;
                case KeyEvent.KEYCODE_DPAD_RIGHT:
                    nativePushKey(262);             // KEY_RIGHT
                    break;
            }
        }
        return super.dispatchKeyEvent(event);
    }
}
