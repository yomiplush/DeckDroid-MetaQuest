/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kgsl_compat.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static int denied = EACCES, wait_error, submit_error, wait_calls;
static unsigned retired;
static int mock(int fd, unsigned long op, void *arg) {
    (void)fd;
    if (op == IOCTL_KGSL_GPU_COMMAND || op == IOCTL_KGSL_GPU_AUX_COMMAND) {
        if (submit_error) { errno = submit_error; return -1; }
        return 0;
    }
    if (op == IOCTL_KGSL_DRAWCTXT_DESTROY || op == IOCTL_KGSL_DRAWCTXT_CREATE) return 0;
    if (op == IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID) {
        if (denied) { errno = denied; return -1; }
        ((struct kgsl_cmdstream_readtimestamp_ctxtid *)arg)->timestamp = 99;
        return 0;
    }
    assert(op == IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID);
    struct kgsl_device_waittimestamp_ctxtid *w = arg;
    assert(w->timeout == 0);
    wait_calls++;
    if (wait_error || w->timestamp > retired) {
        errno = wait_error ? wait_error : ETIMEDOUT;
        return -1;
    }
    return 0;
}
static void submit(int fd, unsigned context, unsigned ts) {
    struct kgsl_gpu_command c = {.context_id = context, .timestamp = ts};
    assert(quest_kgsl_ioctl(mock, fd, IOCTL_KGSL_GPU_COMMAND, &c) == 0);
}
static unsigned query(int fd, unsigned context) {
    struct kgsl_cmdstream_readtimestamp_ctxtid q = {.context_id=context, .type=KGSL_TIMESTAMP_RETIRED};
    assert(quest_kgsl_ioctl(mock, fd, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &q) == 0);
    return q.timestamp;
}
int main(void) {
    submit(42, 7, 10); submit(42, 7, 20); submit(42, 7, 30);
    assert(query(42, 7) == 0);
    retired=20; assert(query(42, 7) == 20);
    wait_error=EACCES; retired=30; assert(query(42, 7) == 20);
    wait_error=0; assert(query(42, 7) == 30);
    assert(query(43, 7) == 0);
    struct kgsl_drawctxt_destroy d = {.drawctxt_id=7};
    assert(quest_kgsl_ioctl(mock, 42, IOCTL_KGSL_DRAWCTXT_DESTROY, &d) == 0);
    assert(query(42, 7) == 0);
    denied=0; assert(query(42, 7) == 99);
    denied=EIO;
    struct kgsl_cmdstream_readtimestamp_ctxtid q = {.context_id=7, .type=KGSL_TIMESTAMP_RETIRED};
    assert(quest_kgsl_ioctl(mock, 42, IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &q) == -1 && errno == EIO);
    denied=EPERM; submit(42, 8, 10); assert(query(42,8)==10);
    q.type=KGSL_TIMESTAMP_QUEUED;
    assert(quest_kgsl_ioctl(mock,42,IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID,&q)==-1 && errno==EPERM);
    struct kgsl_gpu_aux_command aux={.context_id=9,.timestamp=25};
    assert(quest_kgsl_ioctl(mock,42,IOCTL_KGSL_GPU_AUX_COMMAND,&aux)==0);
    assert(query(42,9)==25);
    submit_error=EIO;
    struct kgsl_gpu_command bad={.context_id=9,.timestamp=30};
    assert(quest_kgsl_ioctl(mock,42,IOCTL_KGSL_GPU_COMMAND,&bad)==-1);
    assert(query(42,9)==25);
    assert(wait_calls>0);
    submit_error=0; retired=100;
    for (unsigned id=100; id<140; id++) submit(50,id,10);
    assert(query(50,100)==10);
    struct kgsl_drawctxt_create fresh={.drawctxt_id=100};
    assert(quest_kgsl_ioctl(mock,50,IOCTL_KGSL_DRAWCTXT_CREATE,&fresh)==0);
    retired=0; submit(50,100,1);
    assert(query(50,100)==0);
    retired=1; assert(query(50,100)==1);
    submit(50,100,30); submit(50,100,20);
    retired=20; assert(query(50,100)==20);
    retired=30; assert(query(50,100)==30);
    for (unsigned i=31; i<600; i++) submit(50,100,i);
    retired=599; assert(query(50,100)==599);
    puts("PASS: denied timestamp queries, conservative retirement, fd isolation and context reuse");
}
