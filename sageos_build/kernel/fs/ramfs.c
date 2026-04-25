#include <stddef.h>
#include "console.h"
#include "ramfs.h"
#include "bin_hello.h"
#include "bin_hello_json.h"
#include "bin_hello_kernel.h"

/* -----------------------------------------------------------------------
 * Test SageLang source — embedded as a string constant
 * ----------------------------------------------------------------------- */

static const char test_sage_source[] =
    "# SageLang test for SageOS REPL\n"
    "let x = 42\n"
    "let y = x * 2\n"
    "print(y)\n"
    "let name = \"SageOS\"\n"
    "print(\"Hello, \" + name + \"!\")\n"
    "print(x + y)\n";

static const char test_error_sage_source[] =
    "print(\"Testing division by zero...\")\n"
    "try let a = 10 / 0 catch (e) print(e)\n"
    "print(\"Testing undefined var...\")\n"
    "try print(undefined_var) catch (err) print(err)\n"
    "print(\"Done.\")\n";

/* -----------------------------------------------------------------------
 * File table
 * ----------------------------------------------------------------------- */

typedef struct {
    const char *path;
    const char *content;
    uint64_t    size;       /* 0 = use strlen */
} RamFile;

static const RamFile files[] = {
    {"/etc/motd",    "Welcome to SageOS v0.1.2.\nType help for commands.\n", 0},
    {"/etc/version", "SageOS 0.1.2 modular kernel\n", 0},
    {"/bin/sh",      "Kernel-resident shell\n", 0},
    {"/dev/fb0",     "UEFI GOP framebuffer\n", 0},
    {"/bin/hello",   (const char *)bin_hello, sizeof(bin_hello)},
    {"/proc/input",  "native-i8042-ps2\n", 0},
    {"/etc/test.sage", test_sage_source, 0},
    {"/etc/test_err.sage", test_error_sage_source, 0},
    {"/etc/hello_native.sagec", (const char *)hello_kernel_sagec, sizeof(hello_kernel_sagec)},
};

/* -----------------------------------------------------------------------
 * Lookup
 * ----------------------------------------------------------------------- */

static int eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

const char *ramfs_find(const char *path) {
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (eq(path, files[i].path)) return files[i].content;
    }
    return 0;
}

uint64_t ramfs_find_size(const char *path, const char **out_data) {
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (eq(path, files[i].path)) {
            *out_data = files[i].content;
            if (files[i].size > 0) return files[i].size;
            /* Compute strlen */
            uint64_t len = 0;
            const char *s = files[i].content;
            while (s[len]) len++;
            return len;
        }
    }
    *out_data = 0;
    return 0;
}

void ramfs_ls(void) {
    console_write("\n/");
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        console_write("\n");
        console_write(files[i].path);
    }
}
