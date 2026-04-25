# scrape_cmds.pl — Extracts command-name → handler mappings from shell.c
#
# PURPOSE
#   Scans shell.c for lines of the form:
#       if (starts_word(cmd, "name")) { handler(...); }
#   and emits:
#       name: handler
#   Output can be diffed against cmd_map.txt to verify the dispatch table
#   is consistent with the source after an edit.
#
# USAGE
#   perl scrape_cmds.pl sageos_build/kernel/shell/shell.c
#   perl scrape_cmds.pl sageos_build/kernel/shell/shell.c | diff - cmd_map.txt
#
# GUARD: scrape_cmds.pl-header-v1
while (<>) {
    if (/if \(starts_word\(cmd, "([^"]+)"\)\)\s*\{\s*([^;]+);/) {
        print "$1: $2\n";
    }
}
