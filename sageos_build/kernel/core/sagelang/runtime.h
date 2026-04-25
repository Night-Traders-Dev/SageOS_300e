#ifndef SAGEOS_SAGELANG_RUNTIME_H
#define SAGEOS_SAGELANG_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the SageLang REPL/Runtime
void sage_repl_init(void);

// Execute a single line of SageLang code
void sage_repl_step(const char* line);

// Run a .sage or .sagec file
void sage_run_file(const char* path);

#ifdef __cplusplus
}
#endif

#endif // SAGEOS_SAGELANG_RUNTIME_H
