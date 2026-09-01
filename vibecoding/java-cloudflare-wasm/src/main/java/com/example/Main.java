package com.example;

/*
 * Barebones Java application compiled by TeaVM to WebAssembly GC.
 *
 * The Cloudflare Worker glue calls main() once during module startup.
 * Keep the Java side computation-oriented; HTTP is handled by Worker JS.
 */
public final class Main {
    private Main() {}

    public static String hello() {
        return "Hello from Java -> WebAssembly -> Cloudflare Worker";
    }

    public static int add(int a, int b) {
        return a + b;
    }

    public static void main(String[] args) {
        // Force the class and entry point into the generated Wasm module.
        hello();
    }
}
