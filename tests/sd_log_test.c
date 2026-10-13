/* Production storage/mailbox logic, mocked removable media; no FAT emulation. */
#include <assert.h>
#include <stdio.h>
struct k_msgq;
static void service_wait(struct k_msgq *,int);
#define TEST_MSGQ_WAIT service_wait
#include "../src/sd_log.c"
static bool serve_reads;
static void service_wait(struct k_msgq *q,int timeout) {assert(q==&log_replies && timeout<=3000);if(serve_reads)service_read_request();}
static int mount_error, stat_error, open_error, sync_error;
static unsigned existing, fs_calls, opens, closes, syncs;
static bool partial;
static bool present[128];
static unsigned sizes[128];
void k_msleep(int32_t n) { (void)n; }
int fs_mount(struct fs_mount_t *m) {
 fs_calls++;assert(m->flags & FS_MOUNT_FLAG_NO_FORMAT);
 assert(m->flags & FS_MOUNT_FLAG_USE_DISK_ACCESS);return mount_error;
}
int fs_unmount(struct fs_mount_t *m) { (void)m;fs_calls++;return 0; }
int fs_mkdir(const char *p) { assert(!strcmp(p,LOG_DIRECTORY));fs_calls++;return -EEXIST; }
int fs_stat(const char *p,struct fs_dirent *e) {
 fs_calls++;unsigned n=0;assert(sscanf(p,LOG_DIRECTORY "/LOG%u.TXT",&n)==1);
 e->type=FS_DIR_ENTRY_FILE;e->size=sizes[n];
 return stat_error ? stat_error : (n<existing || present[n]) ? 0 : -ENOENT;
}
int fs_open(struct fs_file_t *f,const char *p,int flags) {
 (void)f;unsigned n=0;assert(sscanf(p,LOG_DIRECTORY "/LOG%u.TXT",&n)==1);
 if(flags==FS_O_READ) { assert(n<existing || present[n]); }
 else {assert(n>=existing && !present[n] && flags==(FS_O_CREATE|FS_O_WRITE));present[n]=true;}
 f->unused=n;f->offset=0;fs_calls++;opens++;return open_error;
}
int fs_sync(struct fs_file_t *f) { (void)f;fs_calls++;syncs++;return sync_error; }
int fs_close(struct fs_file_t *f) { (void)f;fs_calls++;closes++;return 0; }
ssize_t fs_write(struct fs_file_t *f,const void *p,size_t n) { (void)f;(void)p;fs_calls++;return partial ? (ssize_t)n-1 : (ssize_t)n; }
int fs_seek(struct fs_file_t *f,off_t offset,int whence) { assert(whence==FS_SEEK_SET);f->offset=offset;return 0; }
ssize_t fs_read(struct fs_file_t *f,void *data,size_t n) {
 for(unsigned i=0;i<n;i++)((uint8_t *)data)[i]=(f->offset+i)&255;
 return n;
}
static struct adv_log_reply read_request(enum adv_log_operation op,unsigned index,unsigned offset) {
 struct log_request request={.id=42,.file=index,.offset=offset,.op=op};
 assert(!k_msgq_put(&log_requests,&request,K_NO_WAIT));service_read_request();
 struct adv_log_reply reply;assert(!k_msgq_get(&log_replies,&reply,K_NO_WAIT));assert(reply.id==42);return reply;
}
static void reset(void) {
 memset(present,0,sizeof(present));for(unsigned i=0;i<128;i++)sizes[i]=512;
 k_msgq_purge(&log_requests);k_msgq_purge(&log_replies);
 mount_error=stat_error=open_error=sync_error=0;existing=fs_calls=opens=closes=syncs=0;
 partial=opened=mounted=false;next_file=file_bytes=0;
 state=error_code=dropped_bytes=written_bytes=file_number=0;accepting=1;k_msgq_purge(&log_chunks);
}
int main(void) {
 uint8_t data[CHUNK_BYTES]={0};struct adv_sd_log_status status;
 reset();for(unsigned i=0;i<33;i++)assert(enqueue(data,sizeof(data),NULL)==CHUNK_BYTES);
 assert(!fs_calls && log_chunks.count==32 && dropped_bytes==CHUNK_BYTES);
 dropped(NULL,2);assert(!fs_calls && dropped_bytes>CHUNK_BYTES);
 reset();mount_error=-ENODEV;assert(start_storage()==-ENODEV);stop_storage(-ENODEV);
 adv_sd_log_get_status(&status);assert(status.state==ADV_SD_ERROR && status.error==-ENODEV);
 unsigned before=fs_calls;enqueue(data,sizeof(data),NULL);assert(!log_chunks.count && fs_calls==before);
 reset();existing=3;assert(!start_storage() && file_number==3 && opens==1);
 assert(!write_bytes(data,sizeof(data)) && file_bytes==CHUNK_BYTES && written_bytes==CHUNK_BYTES);
 file_bytes=FILE_LIMIT;assert(!write_bytes(data,1) && file_number==4 && closes==1 && syncs==1);
 partial=true;assert(write_bytes(data,10)==-ENOSPC);stop_storage(-ENOSPC);assert(!opened && !mounted);
 reset();existing=LOG_FILES;assert(start_storage()==-ENOSPC && !opens);stop_storage(-ENOSPC);
 reset();stat_error=-EIO;assert(start_storage()==-EIO && !opens);stop_storage(-EIO);
 reset();open_error=-EACCES;assert(start_storage()==-EACCES);stop_storage(-EACCES);
 reset();assert(!start_storage());file_bytes=FILE_LIMIT;sync_error=-EIO;
 assert(write_bytes(data,1)==-EIO && opens==1 && !closes);stop_storage(-EIO);
 assert(write_bytes(data,FILE_LIMIT+1)==-EFBIG);
 reset();existing=3;assert(!start_storage());
 struct adv_log_reply reply=read_request(ADV_LOG_NEXT,1,0);
 assert(!reply.error && reply.file==1 && reply.size==512);
 reply=read_request(ADV_LOG_READ,1,64);assert(!reply.error && reply.length==128 && reply.data[0]==64 && reply.data[127]==191);
 reply=read_request(ADV_LOG_READ,1,512);assert(!reply.error && !reply.length);
 reply=read_request(ADV_LOG_READ,1,513);assert(reply.error==-EINVAL);
 reply=read_request(ADV_LOG_NEXT,4,0);assert(reply.error==-ENOENT);
 sync_error=-EIO;reply=read_request(ADV_LOG_READ,1,0);assert(reply.error==-EIO);
 assert(adv_sd_log_fetch(ADV_LOG_READ,128,0,&reply)==-EINVAL);
 assert(adv_sd_log_fetch(ADV_LOG_READ,0,0,&reply)==-ENODEV);
 sync_error=0;atomic_set(&state,ADV_SD_ACTIVE);serve_reads=true;
 assert(!adv_sd_log_fetch(ADV_LOG_READ,1,64,&reply) && reply.data[0]==64);
 serve_reads=false;assert(adv_sd_log_fetch(ADV_LOG_READ,1,64,&reply)==-ETIMEDOUT);
 service_read_request(); /* Late reply from the timed-out request. */
 serve_reads=true;assert(!adv_sd_log_fetch(ADV_LOG_READ,1,128,&reply) && reply.offset==128 && reply.data[0]==128);
 log_client.locked=true;assert(adv_sd_log_fetch(ADV_LOG_READ,1,0,&reply)==-EBUSY);log_client.locked=false;
 dirty=false;unsigned sync_before=syncs;assert(!sync_if_dirty() && syncs==sync_before);
 dirty=true;sync_error=-EIO;assert(sync_if_dirty()==-EIO && dirty);
 sync_error=0;assert(!sync_if_dirty() && !dirty);
 assert(!k_sem_take(&sd_wake,K_NO_WAIT)); /* Enqueue/request signalled worker. */
 puts("PASS: production SD mailbox never calls FS; bounded overflow; no format/overwrite; mount/stat/open/full/partial/sync errors; rotation");
}
