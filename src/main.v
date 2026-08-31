static func<void> main() {
    print("Testing right now");
}

tooth<void> print(string s) language("C") => |
    if (s != NULL) {
        printf("%s\n", s);
    }
|