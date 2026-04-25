while (<>) {
    if (/if \(starts_word\(cmd, "([^"]+)"\)\)\s*\{\s*([^;]+);/) {
        print "$1: $2\n";
    }
}
