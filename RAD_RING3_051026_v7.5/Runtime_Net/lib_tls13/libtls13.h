#ifndef LIBTLS13_H
#define LIBTLS13_H

#include <stdint.h>
#include <stddef.h>
#include "Runtime_Net/sys_tls13/sys_tls13.h"

static inline int sys_tls13_init(void *session, int socket_fd, const char *hostname) {
    return (int)tls13_syscall(SYS_TLS13_INIT, (uint64_t)session, (uint64_t)socket_fd, (uint64_t)hostname, 0, 0, 0, 0);
}

static inline int sys_tls13_handshake_step(void *session) {
    return (int)tls13_syscall(SYS_TLS13_HANDSHAKE_STEP, (uint64_t)session, 0, 0, 0, 0, 0, 0);
}

static inline int sys_tls13_connect(int socket_fd, const char *hostname) {
    return (int)tls13_syscall(SYS_TLS13_CONNECT, (uint64_t)socket_fd, (uint64_t)hostname, 0, 0, 0, 0, 0);
}

static inline int sys_tls13_record_write(void *session, uint8_t type, const void *payload, size_t len) {
    return (int)tls13_syscall(SYS_TLS13_RECORD_WRITE, (uint64_t)session, (uint64_t)type, (uint64_t)payload, (uint64_t)len, 0, 0, 0);
}

static inline int sys_tls13_record_read(void *session, uint8_t *out_type, void *out_payload, size_t *out_len) {
    return (int)tls13_syscall(SYS_TLS13_RECORD_READ, (uint64_t)session, (uint64_t)out_type, (uint64_t)out_payload, (uint64_t)out_len, 0, 0, 0);
}

static inline int sys_tls13_write(int handle, const void *buf, size_t len) {
    return (int)tls13_syscall(SYS_TLS13_WRITE, (uint64_t)handle, (uint64_t)buf, (uint64_t)len, 0, 0, 0, 0);
}

static inline int sys_tls13_read(int handle, void *buf, size_t max_len) {
    return (int)tls13_syscall(SYS_TLS13_READ, (uint64_t)handle, (uint64_t)buf, (uint64_t)max_len, 0, 0, 0, 0);
}

static inline int sys_tls13_close(int handle) {
    return (int)tls13_syscall(SYS_TLS13_CLOSE, (uint64_t)handle, 0, 0, 0, 0, 0, 0);
}

#endif /* LIBTLS13_H */
