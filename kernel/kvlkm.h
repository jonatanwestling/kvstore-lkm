#ifndef KV_LKM_H
#define KV_LKM_H

struct kv_message msg_struct;

#define LOCAL_IP "172.20.10.4"
#define REMOTE_IP "172.20.10.6"
#define LOCAL_PORT 6666
#define REMOTE_PORT 5555

int init_module(void);
void cleanup_module(void);

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);

int _handle_operation(struct kv_message *msg);
int _handle_insert(struct kv_message *msg);
int _handle_delete(struct kv_message *msg);
int _handle_get(struct kv_message *msg);
int _handle_update(struct kv_message *msg);
int _handle_snapshot(struct kv_snapshot_message *msg);

static int _handle_kv_message(int cmd, void __user *arg);
static int _handle_kv_snapshot(void __user *arg);
static int _handle_kv_count(void __user *arg);

static void init_netpoll(void);

#endif /* KV_LKM_H */
