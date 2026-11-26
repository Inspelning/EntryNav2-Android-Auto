// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
/* PC-only stand-in for the HU's libvmostclient.so.1 (tests/test_evo_wlan.cpp, tests/test_evo_reboot.cpp).
 * Records every message sent into $FAKE_VMOST_LOG and answers like:
 *   - wland: WLANState3 Get -> Status($FAKE_WLAN_STATE); SelectWLANState3 StartResultAck -> ResultAck(handle) then
 *     Status(1). $FAKE_WLAN_SILENT=1: no answer at all.
 *   - the Cortex-M4 to ECUReset (0100.FD.04.C00 Set F4 63 11 01), as $FAKE_ECURESET_MODE says: ok (51 01), pending_ok
 *     (7F 11 78 then 51 01), alt_inst (51 01 on FD.41), nrc22 (7F 11 22), most_error (operation 0xF), silent,
 *     other_tester (51 01 for tester F1), other_address (51 01 from address 0x0200), echo (a copy of the request),
 *     send_refused (vmostMostMsgSend returns -1). Unset: ok.
 * $FAKE_VMOST_FAIL_REGISTER=1 makes vmostFblockShadowRegister return -1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void (*cb_t)(void*, void*);
static cb_t g_cb;
static void* g_user;
static int g_pipe[2] = {-1, -1};
struct out_msg {
    uint16_t addr;
    uint8_t fblock, inst;
    uint16_t fkt, r0;
    uint32_t op;
    uint16_t len, r1;
    const uint8_t* data;
    uint32_t channel;
};
struct queued {
    uint16_t addr;
    uint8_t fblock, inst;
    uint16_t fkt;
    uint32_t op;
    uint16_t len;
    uint8_t data[8];
};
static struct queued g_q[16];
static int g_nq;

static void enqueueFrom(uint16_t addr, uint8_t fb, uint8_t in, uint16_t fkt, uint32_t op, const uint8_t* d,
                        uint16_t len) {
    if (g_nq >= 16) {
        return;
    }
    struct queued* q = &g_q[g_nq++];
    q->addr = addr;
    q->fblock = fb;
    q->inst = in;
    q->fkt = fkt;
    q->op = op;
    q->len = len;
    memcpy(q->data, d, len);
    if (write(g_pipe[1], "x", 1) != 1) {
    }
}
static void enqueue(uint8_t fb, uint8_t in, uint16_t fkt, uint32_t op, const uint8_t* d, uint16_t len) {
    enqueueFrom(0x0100, fb, in, fkt, op, d, len);
}
int vmostConnect(cb_t cb, void* user, int flags) {
    (void)flags;
    g_cb = cb;
    g_user = user;
    g_nq = 0;
    return pipe(g_pipe);
}
int vmostFblockShadowRegister(uint8_t fb, uint8_t in) {
    const char* p = getenv("FAKE_VMOST_LOG");
    FILE* f = p ? fopen(p, "a") : 0;
    if (f) {
        fprintf(f, "register %02x.%02x\n", fb, in);
        fclose(f);
    }
    return getenv("FAKE_VMOST_FAIL_REGISTER") ? -1 : 0;
}
int vmostFblockShadowDeregister(uint8_t fb, uint8_t in) {
    const char* p = getenv("FAKE_VMOST_LOG");
    FILE* f = p ? fopen(p, "a") : 0;
    if (f) {
        fprintf(f, "deregister %02x.%02x\n", fb, in);
        fclose(f);
    }
    return 0;
}
int vmostFileDescriptorGet(int* fd) {
    *fd = g_pipe[0];
    return 0;
}
int vmostDisconnect(void) {
    close(g_pipe[0]);
    close(g_pipe[1]);
    g_pipe[0] = g_pipe[1] = -1;
    return 0;
}
// The M4's answer to ECUReset, as $FAKE_ECURESET_MODE says; -1 when the sending is refused.
static int answerEcuReset(const struct out_msg* m) {
    static const uint8_t ok[4] = {0xF4, 0x63, 0x51, 0x01};
    static const uint8_t pending[5] = {0xF4, 0x63, 0x7F, 0x11, 0x78};
    static const uint8_t refused[5] = {0xF4, 0x63, 0x7F, 0x11, 0x22};
    static const uint8_t mostError[2] = {0x02, 0x01};
    static const uint8_t otherTester[4] = {0xF1, 0x63, 0x51, 0x01};
    const char* mode = getenv("FAKE_ECURESET_MODE");
    if (!mode || strcmp(mode, "ok") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "pending_ok") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, pending, 5);
        enqueue(0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "alt_inst") == 0) {
        enqueue(0xFD, 0x41, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "nrc22") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, refused, 5);
    } else if (strcmp(mode, "most_error") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xF, mostError, 2);
    } else if (strcmp(mode, "other_tester") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, otherTester, 4);
    } else if (strcmp(mode, "other_address") == 0) {
        enqueueFrom(0x0200, 0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "echo") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0x0, m->data, m->len);
    } else if (strcmp(mode, "send_refused") == 0) {
        return -1;
    }
    return 0;
}
int vmostMostMsgSend(void* msg) {
    const struct out_msg* m = (const struct out_msg*)msg;
    const char* p = getenv("FAKE_VMOST_LOG");
    FILE* f = p ? fopen(p, "a") : 0;
    if (f) {
        fprintf(f, "send addr=%04x %02x.%02x.%03x op=%u len=%u :", m->addr, m->fblock, m->inst, m->fkt, m->op, m->len);
        for (unsigned i = 0; i < m->len; ++i) {
            fprintf(f, " %02x", m->data[i]);
        }
        fprintf(f, "\n");
        fclose(f);
    }
    if (getenv("FAKE_WLAN_SILENT")) {
        return 0;
    }
    if (m->fblock == 0xA0 && m->inst == 1 && m->fkt == 0xC10 && m->op == 1) {
        const char* s = getenv("FAKE_WLAN_STATE");
        uint8_t st = (uint8_t)(s ? atoi(s) : 0);
        enqueue(0xA0, 1, 0xC10, 0xC, &st, 1);
    } else if (m->fblock == 0xA0 && m->inst == 1 && m->fkt == 0xC11 && m->op == 6 && m->len == 3) {
        uint8_t one = 1;
        enqueue(0xA0, 1, 0xC11, 0xD, m->data, 2);
        enqueue(0xA0, 1, 0xC10, 0xC, &one, 1);
    } else if (m->addr == 0x0100 && m->fblock == 0xFD && m->inst == 4 && m->fkt == 0xC00 && m->op == 0 && m->len == 4 &&
               m->data[0] == 0xF4 && m->data[1] == 0x63 && m->data[2] == 0x11 && m->data[3] == 0x01) {
        return answerEcuReset(m);
    }
    return 0;
}
int vmostProcessMessages(void) {
    char b[16];
    if (read(g_pipe[0], b, sizeof(b)) < 0) {
        return -1;
    }
    int n = g_nq;
    g_nq = 0;
    for (int i = 0; i < n; ++i) {
        unsigned char raw[32];
        memset(raw, 0, sizeof(raw));
        const uint8_t* dp = g_q[i].data;
        memcpy(raw, &g_q[i].addr, 2);
        raw[2] = g_q[i].fblock;
        raw[3] = g_q[i].inst;
        memcpy(raw + 4, &g_q[i].fkt, 2);
        memcpy(raw + 8, &g_q[i].op, 4);
        memcpy(raw + 12, &g_q[i].len, 2);
        memcpy(raw + 16, &dp, sizeof(dp));
        g_cb(raw, g_user);
    }
    return 0;
}
