#include <stdint.h>
#include "console.h"
#include "../../sage_lang/include/compiler.h"

void sage_runtime_init(void) {
    console_write("\nSageLang runtime initialized.");
}

void sage_execute(const char *module_name) {
    console_write("\nSageLang execution requested for module: ");
    console_write(module_name);
}

int sage_compile(const char *source_path, const char *output_path) {
    console_write("\nSageLang compiling: ");
    console_write(source_path);
    return 0;
}
