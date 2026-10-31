/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kgsl_compat.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#if __SIZEOF_POINTER__ == 8
_Static_assert(sizeof(struct kgsl_gpumem_alloc_id) == 48, "KGSL alloc ABI");
_Static_assert(sizeof(struct kgsl_gpuobj_alloc) == 48, "GPUOBJ alloc ABI");
_Static_assert(sizeof(struct kgsl_gpuobj_free) == 32, "GPUOBJ free ABI");
_Static_assert(sizeof(struct kgsl_gpuobj_info) == 48, "GPUOBJ info ABI");
#endif

static void report(const char *operation, int error) {
    static unsigned successes, failures;
    unsigned *counter = error ? &failures : &successes;
    if (__atomic_fetch_add(counter, 1, __ATOMIC_RELAXED) >= (error ? 8u : 1u)) return;
    int saved = errno;
#ifdef __ANDROID__
    __android_log_print(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "QuestKGSL",
                        "%s: GPUOBJ compatibility path, errno=%d", operation, error);
#else
    fprintf(stderr, "[QuestKGSL] %s: GPUOBJ compatibility path, errno=%d\n", operation, error);
#endif
    errno = saved;
}

#define TIMESTAMP_RING 512
struct timestamp_context {
    struct timestamp_context *next;
    int fd;
    unsigned id, retired, count;
    bool retired_valid;
    unsigned ring[TIMESTAMP_RING];
};
static struct timestamp_context *timestamps;
static int timestamp_lock, timestamp_tracking_failed;
static void lock_timestamps(void) {
    while (__atomic_exchange_n(&timestamp_lock, 1, __ATOMIC_ACQUIRE)) sched_yield();
}
static void unlock_timestamps(void) { __atomic_store_n(&timestamp_lock, 0, __ATOMIC_RELEASE); }
static struct timestamp_context *find_timestamp(int fd, unsigned id) {
    for (struct timestamp_context *c = timestamps; c; c = c->next)
        if (c->fd == fd && c->id == id) return c;
    return NULL;
}
static void forget_timestamp(int fd, unsigned id) {
    lock_timestamps();
    struct timestamp_context **link = &timestamps;
    while (*link) {
        struct timestamp_context *c = *link;
        if (c->fd == fd && c->id == id) {
            *link = c->next;
            free(c);
            break;
        }
        link = &c->next;
    }
    unlock_timestamps();
}
static void record_timestamp(int fd, unsigned id, unsigned timestamp) {
    lock_timestamps();
    struct timestamp_context *c = find_timestamp(fd, id);
    if (!c) {
        c = calloc(1, sizeof(*c));
        if (!c) {
            timestamp_tracking_failed = 1;
            unlock_timestamps();
            return;
        }
        c->fd = fd; c->id = id; c->next = timestamps; timestamps = c;
    }
    if (c->retired_valid && (int32_t)(timestamp - c->retired) <= 0) {
        unlock_timestamps();
        return;
    }
    if (c->count == TIMESTAMP_RING) {
        memmove(c->ring, c->ring + 1, (--c->count) * sizeof(c->ring[0]));
    }
    unsigned i = c->count;
    /* Concurrent submissions can return to userspace in a different order. */
    while (i && (int32_t)(timestamp - c->ring[i - 1]) < 0) {
        c->ring[i] = c->ring[i - 1];
        i--;
    }
    c->ring[i] = timestamp;
    c->count++;
    unlock_timestamps();
}
/* Horizon OS denies READTIMESTAMP; only a successful zero-timeout wait proves retirement. */
static int read_timestamp(quest_ioctl_fn real, int fd, struct kgsl_cmdstream_readtimestamp_ctxtid *q) {
    lock_timestamps();
    if (timestamp_tracking_failed) {
        unlock_timestamps();
        errno = ENOMEM;
        return -1;
    }
    struct timestamp_context *c = find_timestamp(fd, q->context_id);
    if (c && c->count) {
        int low = -1, high = (int)c->count - 1;
        while (low < high) {
            int mid = (low + high + 1) / 2;
            struct kgsl_device_waittimestamp_ctxtid w = {
                .context_id = c->id, .timestamp = c->ring[mid], .timeout = 0};
            if (real(fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID, &w) == 0) low = mid;
            else high = mid - 1;
        }
        if (low >= 0) {
            c->retired = c->ring[low];
            c->retired_valid = true;
            c->count -= low + 1;
            memmove(c->ring, c->ring + low + 1, c->count * sizeof(c->ring[0]));
        }
    }
    q->timestamp = c ? c->retired : 0;
    unlock_timestamps();
    report("timestamp query emulated", 0);
    return 0;
}

int quest_kgsl_ioctl(quest_ioctl_fn real, int fd, unsigned long request, void *arg) {
    int rc = real(fd, request, arg);
    if (rc == 0 && arg) {
        if (request == IOCTL_KGSL_GPU_COMMAND) {
            const struct kgsl_gpu_command *c = arg;
            record_timestamp(fd, c->context_id, c->timestamp);
        } else if (request == IOCTL_KGSL_GPU_AUX_COMMAND) {
            const struct kgsl_gpu_aux_command *c = arg;
            record_timestamp(fd, c->context_id, c->timestamp);
        } else if (request == IOCTL_KGSL_DRAWCTXT_DESTROY) {
            forget_timestamp(fd, ((struct kgsl_drawctxt_destroy *)arg)->drawctxt_id);
        } else if (request == IOCTL_KGSL_DRAWCTXT_CREATE) {
            forget_timestamp(fd, ((struct kgsl_drawctxt_create *)arg)->drawctxt_id);
        }
    }
    if (rc != -1 || (errno != EACCES && errno != EPERM) || arg == NULL) return rc;
    if (request == IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID &&
        ((struct kgsl_cmdstream_readtimestamp_ctxtid *)arg)->type == KGSL_TIMESTAMP_RETIRED) {
        report("timestamp read denied; using nonblocking wait", errno);
        return read_timestamp(real, fd, arg);
    }

    if (request == IOCTL_KGSL_GPUMEM_ALLOC_ID) {
        struct kgsl_gpumem_alloc_id *old = arg;
        struct kgsl_gpuobj_alloc alloc = {.size = old->size, .flags = old->flags};
        rc = real(fd, IOCTL_KGSL_GPUOBJ_ALLOC, &alloc);
        if (rc != 0) { report("alloc failed", errno); return rc; }
        struct kgsl_gpuobj_info info = {.id = alloc.id};
        rc = real(fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
        int error = errno;
        if (rc == 0 && (info.size > SIZE_MAX || alloc.mmapsize > SIZE_MAX ||
                       info.gpuaddr > ULONG_MAX || info.flags > UINT_MAX)) {
            rc = -1;
            error = EOVERFLOW;
        }
        if (rc != 0) {
            struct kgsl_gpuobj_free release = {.id = alloc.id};
            if (real(fd, IOCTL_KGSL_GPUOBJ_FREE, &release) != 0)
                report("cleanup failed", errno);
            errno = error;
            report("info failed", error);
            return rc;
        }
        old->id = alloc.id;
        old->flags = (unsigned int)info.flags;
        old->size = info.size;
        old->mmapsize = alloc.mmapsize;
        old->gpuaddr = info.gpuaddr;
        report("allocation succeeded", 0);
        return 0;
    }
    if (request == IOCTL_KGSL_GPUMEM_FREE_ID) {
        const struct kgsl_gpumem_free_id *old = arg;
        struct kgsl_gpuobj_free release = {.id = old->id};
        rc = real(fd, IOCTL_KGSL_GPUOBJ_FREE, &release);
        if (rc != 0) report("free failed", errno);
    }
    return rc;
}
