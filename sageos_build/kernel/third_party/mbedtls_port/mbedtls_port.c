#include <mbedtls/platform.h>
#include <mbedtls/entropy.h>
#include <mbedtls/debug.h>
#include <psa/crypto.h>
#include "sage_libc_shim.h"
#include "dmesg.h"
#include "timer.h"
#include "lwip/mem.h"

/* mbed TLS platform functions */

static void *mbedtls_port_calloc(size_t n, size_t size) {
    void *ptr = mem_malloc((mem_size_t)(n * size));
    if (ptr) {
        sage_memset(ptr, 0, n * size);
    }
    return ptr;
}

static void mbedtls_port_free(void *ptr) {
    if (ptr) {
        mem_free(ptr);
    }
}

void mbedtls_platform_exit_alt(int status) {
    (void)status;
    // Just loop or do nothing in kernel
    for(;;);
}

/* Hardware entropy source */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    uint64_t t;
    size_t i = 0;
    while (i < len) {
        t = timer_ticks();
        // Extract some entropy from low bits of timer
        output[i++] = (unsigned char)(t & 0xFF);
        output[i++] = (unsigned char)((t >> 8) & 0xFF);
        // We really should use RDRAND if available, but this is a fallback
    }
    *olen = (i > len) ? len : i;
    return 0;
}

void mbedtls_port_init(void) {
    /* Configure MbedTLS to use persistent lwIP memory functions */
    mbedtls_platform_set_calloc_free(mbedtls_port_calloc, mbedtls_port_free);
    
    /* Set debug threshold to maximum for deep diagnostics */
    mbedtls_debug_set_threshold(4);
    
    /* Initialize PSA Crypto - REQUIRED for TLS 1.3 in MbedTLS 3.x */
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        dmesg_printf("PSA: initialization failed (status=%d)", (int)status);
    } else {
        dmesg_log("PSA: initialization successful");
    }
}
