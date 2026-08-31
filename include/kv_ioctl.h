#ifndef KV_IOCTL_H
#define KV_IOCTL_H

// no conflict with g according to kernel.org
#define KV_MAGIC 'g'

// Ioctl commands
#define KV_INSERT  _IOW(KV_MAGIC, 1, struct kv_message)
#define KV_DELETE  _IOW(KV_MAGIC, 2, struct kv_message)
#define KV_GET     _IOWR(KV_MAGIC, 3, struct kv_message)
#define KV_UPDATE  _IOW(KV_MAGIC, 4, struct kv_message)
#define KV_SNAPSHOT _IOWR(KV_MAGIC, 5, struct kv_snapshot_message)
#define KV_COUNT   _IOR(KV_MAGIC, 6, unsigned int)

#define SUCCESS 0
#define ERROR 1
#define PARTIAL 2

// Maximum lengths value
#define MAX_VALUE_LEN 64

// The message struct
struct kv_message {
    int status;
    unsigned long long key;
    char value[MAX_VALUE_LEN];
};

struct kv_snapshot_message {
    int status;
    unsigned int capacity;
    unsigned int count;
    struct kv_message entries[];
};

#endif
