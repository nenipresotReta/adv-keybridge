/* SPDX-License-Identifier: MIT
 * The Zephyr backend formats into a bounded nonblocking mailbox. Only sd_thread
 * may mount/write/sync. No FS calls in a log callback, ISR, USB owner or UI.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/atomic.h>
#include "adv_sd_log.h"
#include "adv_battery_history.h"

LOG_MODULE_REGISTER(adv_sd_log, LOG_LEVEL_INF);
#define CHUNK_BYTES 256
#define LOG_FILES 128
#define FILE_LIMIT (1024u * 1024u)
#define LOG_DIRECTORY "/SD:/ADVLOG"
struct log_chunk { uint16_t length; uint8_t data[CHUNK_BYTES]; };
K_MSGQ_DEFINE(log_chunks, sizeof(struct log_chunk), 32, 4);
static atomic_t state, error_code, dropped_bytes, written_bytes, file_number;
static atomic_t accepting = 1;
static atomic_t sleep_hold, sleep_ack, storage_stopped;
static FATFS fatfs;
static struct fs_mount_t mount = {.type=FS_FATFS, .fs_data=&fatfs,
    .mnt_point="/SD:", .flags=FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS};
static struct fs_file_t file;
static bool opened, mounted, dirty;
K_SEM_DEFINE(sd_wake,0,1);
void adv_sd_log_sleep_hold(bool hold) {
    atomic_set(&sleep_hold,hold);
    if(hold)atomic_clear(&accepting);
    k_sem_give(&sd_wake);
}
int adv_sd_log_sleep_status(void) {
    if(atomic_get(&storage_stopped)) {
        int ack=atomic_get(&sleep_ack);return ack<0?ack:1;
    }
    return atomic_get(&sleep_ack);
}
static uint32_t file_bytes;
static unsigned next_file;
struct log_request { uint32_t id, file, offset; enum adv_log_operation op; };
K_MSGQ_DEFINE(log_requests,sizeof(struct log_request),1,4);
K_MSGQ_DEFINE(log_replies,sizeof(struct adv_log_reply),1,4);
K_MUTEX_DEFINE(log_client);
static uint32_t request_id; /* Protected by log_client. */

int adv_sd_log_fetch(enum adv_log_operation op, uint32_t index, uint32_t offset,
                     struct adv_log_reply *reply) {
    if (!reply || index>=LOG_FILES || offset>FILE_LIMIT ||
        (op!=ADV_LOG_NEXT && op!=ADV_LOG_READ)) return -EINVAL;
    if (atomic_get(&state)!=ADV_SD_ACTIVE) return -ENODEV;
    if (k_mutex_lock(&log_client,K_NO_WAIT)) return -EBUSY;
    struct log_request request={.id=++request_id,.file=index,.offset=offset,.op=op};
    k_msgq_purge(&log_replies);
    int err=k_msgq_put(&log_requests,&request,K_NO_WAIT);
    if(!err)k_sem_give(&sd_wake);
    int64_t deadline=k_uptime_get()+3000;
    while (!err) {
        int64_t remaining=deadline-k_uptime_get();
        if (remaining<=0 || k_msgq_get(&log_replies,reply,K_MSEC(remaining))) {
            err=-ETIMEDOUT;break;
        }
        if (reply->id==request.id) { err=reply->error;break; }
        /* A previous timed-out request may finish late. Never return its data. */
    }
    k_mutex_unlock(&log_client);
    return err;
}

static void service_read_request(void) {
    struct log_request request;
    if (k_msgq_get(&log_requests,&request,K_NO_WAIT)) return;
    struct adv_log_reply reply={.id=request.id,.file=request.file,.offset=request.offset};
    if (!mounted || !opened) { reply.error=-ENODEV;goto done; }
    /* Sync the active file before exposing its size/content. */
    reply.error=fs_sync(&file);
    if(!reply.error)dirty=false;
    if (reply.error) goto done;
    for (;reply.file<LOG_FILES;reply.file++) {
        char path[40];struct fs_dirent entry;
        snprintf(path,sizeof(path),LOG_DIRECTORY "/LOG%05u.TXT",reply.file);
        reply.error=fs_stat(path,&entry);
        if (reply.error==-ENOENT && request.op==ADV_LOG_NEXT) { k_msleep(1);continue; }
        if (reply.error) break;
        if (entry.type!=FS_DIR_ENTRY_FILE || entry.size>FILE_LIMIT) {
            reply.error=-EINVAL;break;
        }
        reply.size=entry.size;
        if (request.op==ADV_LOG_NEXT) break;
        if (reply.offset>reply.size) { reply.error=-EINVAL;break; }
        struct fs_file_t reader;fs_file_t_init(&reader);
        reply.error=fs_open(&reader,path,FS_O_READ);
        if (reply.error) break;
        reply.error=fs_seek(&reader,reply.offset,FS_SEEK_SET);
        if (!reply.error) {
            ssize_t n=fs_read(&reader,reply.data,MIN(ADV_LOG_CHUNK,reply.size-reply.offset));
            if (n<0) reply.error=(int)n;else reply.length=(uint16_t)n;
        }
        int closed=fs_close(&reader);
        if (!reply.error) reply.error=closed;
        break;
    }
    if (reply.file==LOG_FILES) reply.error=-ENOENT;
done:
    /* Replies are values, never pointers into a caller that may time out. */
    k_msgq_purge(&log_replies);
    (void)k_msgq_put(&log_replies,&reply,K_NO_WAIT);
}

void adv_sd_log_get_status(struct adv_sd_log_status *status) {
    *status=(struct adv_sd_log_status){.state=atomic_get(&state),
        .error=atomic_get(&error_code), .dropped=atomic_get(&dropped_bytes),
        .bytes=atomic_get(&written_bytes), .file=atomic_get(&file_number)};
}
static int enqueue(uint8_t *data, size_t length, void *ctx) {
    ARG_UNUSED(ctx);
    if (!atomic_get(&accepting)) return (int)length;
    size_t offset=0;
    while (offset<length) {
        struct log_chunk chunk;
        chunk.length=MIN(length-offset,CHUNK_BYTES);
        memcpy(chunk.data,data+offset,chunk.length);
        if (k_msgq_put(&log_chunks,&chunk,K_NO_WAIT))
            atomic_add(&dropped_bytes,chunk.length);
        offset+=chunk.length;
    }
    k_sem_give(&sd_wake);
    return (int)length; /* Logging must never wait for removable storage. */
}
static uint8_t format_buffer[CHUNK_BYTES];
LOG_OUTPUT_DEFINE(sd_output,enqueue,format_buffer,sizeof(format_buffer));
static void process(const struct log_backend *backend, union log_msg_generic *msg) {
    ARG_UNUSED(backend);
    if (atomic_get(&accepting))
        log_output_msg_process(&sd_output,&msg->log,LOG_OUTPUT_FLAG_LEVEL |
            LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP |
            LOG_OUTPUT_FLAG_CRLF_LFONLY);
}
static void panic(const struct log_backend *backend) {
    ARG_UNUSED(backend);
    /* Do not touch SPI/FS in a panic; they may already hold locks. */
    log_output_flush(&sd_output);
}
static void dropped(const struct log_backend *backend, uint32_t count) {
    ARG_UNUSED(backend);
    char note[80];
    int n=snprintf(note,sizeof(note),"\n[SD LOG] Zephyr dropped %u messages\n",count);
    enqueue((uint8_t *)note,(size_t)n,NULL);
}
static const struct log_backend_api backend_api={.process=process,.panic=panic,.dropped=dropped};
LOG_BACKEND_DEFINE(adv_sd_backend,backend_api,true);

static int open_next(void) {
    struct fs_dirent entry;
    for (; next_file<LOG_FILES; next_file++) {
        char path[40];
        snprintf(path,sizeof(path),LOG_DIRECTORY "/LOG%05u.TXT",next_file);
        int err=fs_stat(path,&entry);
        if (!err) continue; /* Never truncate/reuse an existing user file. */
        if (err!=-ENOENT) return err;
        fs_file_t_init(&file);
        err=fs_open(&file,path,FS_O_CREATE | FS_O_WRITE);
        if (err) return err;
        opened=true;file_bytes=0;
        atomic_set(&file_number,next_file++);
        return 0;
    }
    return -ENOSPC; /* Bounded retention; no automatic deletion. */
}
static int start_storage(void) {
    int err=fs_mount(&mount);
    if (err) return err;
    mounted=true;
    err=fs_mkdir(LOG_DIRECTORY);
    if (err && err!=-EEXIST) return err;
    return open_next();
}
static int write_bytes(const void *data, size_t length) {
    if (length>FILE_LIMIT) return -EFBIG;
    if (file_bytes+length>FILE_LIMIT) {
        int err=fs_sync(&file);
        if (err) return err;
        err=fs_close(&file);opened=false;
        if (err) return err;
        err=open_next();
        if (err) return err;
    }
    ssize_t n=fs_write(&file,data,length);
    if (n>0) { dirty=true;file_bytes+=(uint32_t)n;atomic_add(&written_bytes,n); }
    if (n<0) return (int)n;
    return (size_t)n==length ? 0 : -ENOSPC;
}
static void stop_storage(int error) {
    adv_history_storage_stop(error);
    atomic_clear(&accepting);
    atomic_set(&error_code,error);
    atomic_set(&state,ADV_SD_ERROR);
    k_msgq_purge(&log_chunks);
    /* Best-effort cleanup stays on this thread, including a removed card. */
    if (opened) { (void)fs_close(&file);opened=false; }
    if (mounted) { (void)fs_unmount(&mount);mounted=false; }
    atomic_set(&storage_stopped,1);
    LOG_ERR("SD log stopped: %d; other services continue",error);
}
static int sync_if_dirty(void) {
    if(!dirty)return 0;
    int err=fs_sync(&file);
    if(!err)dirty=false;
    return err;
}
static void sd_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
    int err=start_storage();
    adv_history_storage_start(err);
    if (err) { stop_storage(err);return; }
    atomic_set(&state,ADV_SD_ACTIVE);
    LOG_INF("SD log active: /ADVLOG, file=%ld; sync 2s when dirty; no automatic format/delete",
            (long)atomic_get(&file_number));
    static const char marker[]="\n=== ADV KeyBridge boot log ===\n";
    err=write_bytes(marker,sizeof(marker)-1);
    int64_t next_sync=k_uptime_get()+2000;
    uint32_t reported_dropped=0;
    while (!err) {
        if(atomic_get(&sleep_hold) && !k_msgq_num_used_get(&log_chunks)) {
            err=adv_history_storage_flush();
            if(!err)err=sync_if_dirty();
            atomic_set(&sleep_ack,err?err:1);
            while(!err && atomic_get(&sleep_hold))k_sem_take(&sd_wake,K_SECONDS(1));
            if(!err) { atomic_clear(&sleep_ack);atomic_set(&accepting,1); }
            continue;
        }
        if(!atomic_get(&sleep_hold))atomic_set(&accepting,1);
        adv_history_storage_tick();
        service_read_request();
        struct log_chunk chunk;
        if (!k_msgq_get(&log_chunks,&chunk,K_NO_WAIT))
            err=write_bytes(chunk.data,chunk.length);
        uint32_t lost=(uint32_t)atomic_get(&dropped_bytes);
        if (!err && lost!=reported_dropped) {
            char note[96];
            int n=snprintf(note,sizeof(note),"\n[SD LOG] queue overflow: total %u bytes lost\n",lost);
            err=write_bytes(note,(size_t)n);reported_dropped=lost;
        }
        if (!err && k_uptime_get()>=next_sync) {
            err=sync_if_dirty();next_sync=k_uptime_get()+2000;
        }
        /* Even an always-full log queue must leave room for other services. */
        if(k_msgq_num_used_get(&log_chunks))k_msleep(1);
        else {
            int64_t remaining=next_sync-k_uptime_get();
            (void)k_sem_take(&sd_wake,K_MSEC(MAX(1,remaining)));
        }
    }
    stop_storage(err);
}
K_THREAD_DEFINE(adv_sd_writer,4096,sd_thread,NULL,NULL,NULL,12,0,500);
