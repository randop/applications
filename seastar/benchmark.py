#!/usr/bin/env python3

import random
import string
import time
import sys


def generate_random_text(length=100):
    return "".join(random.choices(string.ascii_letters + string.digits + " ", k=length))


def fibonacci(n):
    if n <= 1:
        return n
    a, b = 0, 1
    for _ in range(n - 1):
        a, b = b, a + b
    return b


def main():
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    start = time.time()
    iteration = 0

    while time.time() - start < duration:
        text = generate_random_text()
        fib_result = fibonacci(30)
        print(f"[{iteration}] {text} fib(30)={fib_result}", flush=True)
        time.sleep(0.5)
        iteration += 1

    print("PROCESS_COMPLETE", flush=True)


if __name__ == "__main__":
    main()
