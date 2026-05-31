#include <stdio.h>
#include <string.h>
#include "ipc_user.h"

/* Minimal IPC test program for SageOS */

int main() {
    printf("[IPC TEST] Starting...\n");

    ipc_handle_t local, peer;
    int r = ipc_channel_create(&local, &peer);
    if (r != 0) {
        printf("[IPC TEST] FAILED to create channel: %d\n", r);
        return 1;
    }
    printf("[IPC TEST] Channel created: local=%d, peer=%d\n", (int)local, (int)peer);

    const char *msg_text = "Hello SageOS IPC!";
    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = 0x1234;
    msg.data = msg_text;
    msg.len = strlen(msg_text);

    r = ipc_send(local, &msg);
    if (r != 0) {
        printf("[IPC TEST] FAILED to send message: %d\n", r);
        return 1;
    }
    printf("[IPC TEST] Message sent to local handle\n");

    char buf[128];
    ipc_message_t recv_msg;
    memset(&recv_msg, 0, sizeof(recv_msg));
    recv_msg.data = buf;
    recv_msg.len = sizeof(buf);

    r = ipc_recv(peer, &recv_msg, sizeof(buf));
    if (r != 0) {
        printf("[IPC TEST] FAILED to receive message: %d\n", r);
        return 1;
    }

    buf[recv_msg.len] = '\0';
    printf("[IPC TEST] Message received from peer: type=0x%x, len=%d, data='%s'\n",
           (unsigned int)recv_msg.type, (int)recv_msg.len, buf);

    if (strcmp(buf, msg_text) == 0) {
        printf("[IPC TEST] SUCCESS!\n");
    } else {
        printf("[IPC TEST] DATA MISMATCH!\n");
        return 1;
    }

    return 0;
}
