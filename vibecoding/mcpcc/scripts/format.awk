BEGIN { in_json = 0; brace_count = 0 }

/^[[:space:]]*json [a-zA-Z_][a-zA-Z0-9_]* = \{/ {
    print "// clang-format off /*JSON*/"
    print
    in_json = 1
    brace_count = 1
    next
}

in_json {
    print
    for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        if (c == "{") brace_count++
        if (c == "}") brace_count--
    }
    if (brace_count == 0) {
        print "// clang-format on"
        in_json = 0
    }
    next
}

{ print }