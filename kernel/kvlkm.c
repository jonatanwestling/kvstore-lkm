/*
 * kvlkm.c: A Key Value store in the Linux Kernel Module
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netpoll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "kv_hash.h"
#include "kv_ioctl.h"
#include "kvlkm.h"

#define SUCCESS 0
// Appears in /dev/kv_store
#define DEVICE_NAME "kv_store"
// Maxlen of msg
#define BUF_LEN 80

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Communicate between userspace and kernel space");
MODULE_AUTHOR("c22jwg, c22aer");

/*
 * Global variables are declared as static, so are global within the file.
 */
static char msg[BUF_LEN];
static char *msg_Ptr;

static dev_t dev;
static struct cdev kv_cdev;
static struct class *kv_class;

// The hash table
static struct hash_table kv_store;

// File operations
static int device_open(struct inode *inode, struct file *file);
static int device_release(struct inode *inode, struct file *file);
static long kv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = kv_ioctl,
};

static const unsigned char remote_mac[6] = {
 0x6a, 0x21, 0xa0, 0x80, 0x62, 0x28};
// The netpoll struct
static struct netpoll np = {
    .name = "kvlkm",
    .dev_name = "enp0s8",
    .local_port = LOCAL_PORT,
    .remote_port = REMOTE_PORT,
};

// Set device node permissions to 0666
static char *kv_devnode(const struct device *dev, umode_t *mode) {
    if (mode) {
        *mode = 0666;
    }
    return NULL;
}

/*
 * This function is called when the module is loaded and this function
 * registers the character device.
 */
int init_module(void) {
    // Register the character device
    if (alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_ALERT "Failed to allocate a major number\n");
        return -1;
    }
    // Initialize and add the cdev structure
    cdev_init(&kv_cdev, &fops);
    // Set the owner field
    if (cdev_add(&kv_cdev, dev, 1) < 0) {
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Failed to add cdev\n");
        return -1;
    }

    // Create the device class
    kv_class = class_create(DEVICE_NAME);
    if (IS_ERR(kv_class)) {
        cdev_del(&kv_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Failed to create class\n");
        return -1;
    }
    // Set the device node permissions
    kv_class->devnode = kv_devnode;

    if (device_create(kv_class, NULL, dev, NULL, DEVICE_NAME) == NULL) {
        class_destroy(kv_class);
        cdev_del(&kv_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Failed to create device\n");
        return -1;
    }

    printk(KERN_INFO "KV Store Module loaded with device major number %d\n",
           MAJOR(dev));

    // Init the hash table

    int ret = kv_hash_init(&kv_store, 10);
    if (ret < 0) {
        device_destroy(kv_class, dev);
        class_destroy(kv_class);
        cdev_del(&kv_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Failed to initialize hash table\n");
        return -1;
    }

    init_netpoll();

    return 0;
}

static void init_netpoll(void) {
    np.local_ip.ip = in_aton(LOCAL_IP);
    np.remote_ip.ip = in_aton(REMOTE_IP);
    memcpy(np.remote_mac, remote_mac, sizeof(np.remote_mac));

    int ret = netpoll_setup(&np);
    if (ret < 0) {
        printk(KERN_ERR "netpoll setup failed: %d\n", ret);
        return;
    }
    const char *message = "kvlkm initialized\n";
    ret = netpoll_send_udp(&np, message, strlen(message));
    if (ret < 0) {
        printk(KERN_ERR "netpoll send udp failed: %d\n", ret);
    }
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void) {
    // destroy the hash
    kv_hash_destroy(&kv_store);

    // unregister the device
    device_destroy(kv_class, dev);
    class_destroy(kv_class);
    cdev_del(&kv_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "KV Store unloaded\n");
    // Send close debug message
    const char *message = "KV Store unloaded\n";
    int res = netpoll_send_udp(&np, message, strlen(message));
    if (res < 0) {
        printk(KERN_ERR "netpoll send udp failed: %d\n", res);
    }
    netpoll_cleanup(&np);
}

/**
 * kv_ioctl - Handle ioctl calls from userspace. This is the main entry point
 * for all operations from userspace, get/insert/update/delete and snapshot.
 * This function dispatches the calls to different handelers based on the cmd
 * argument which dictates the type of data to be copied from/to userspace.
 * @file: Pointer to the file structure (not used in this implementation)
 * @cmd: The ioctl command indicating the type of operation (e.g., KV_INSERT,
 * KV_GET, KV_SNAPSHOT, etc.)
 * @arg: The argument passed from userspace, which is a pointer to a structure
 * (e.g., kv_message or kv_snapshot_message) that contains the data for the
 * operation.
 */
long kv_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    if (cmd == KV_SNAPSHOT) {
        return _handle_kv_snapshot((void __user *)arg);
    } else if (cmd == KV_COUNT) {
        return _handle_kv_count((void __user *)arg);
    } else {
        return _handle_kv_message(cmd, (void __user *)arg);
    }
}

/**
 * _handle_kv_count - Handle the KV_COUNT ioctl command to get the count of
 * key-value pairs in the hash table. This function retrieves the count from the
 * hash table and copies it back to userspace. This function is used by the
 * userspace daemon to determine how much memory it needs to allocate for the
 * snapshot operation.
 * @arg: The argument passed from userspace. It is a pointer to an unsigned int
 * where the count will be stored. Returns 0 on success, negative error code on
 * failure
 */
static int _handle_kv_count(void __user *arg) {
    int count = kv_hash_count(&kv_store);
    unsigned int out_count;

    if (count < 0)
        return count;

    out_count = (unsigned int)count;
    if (copy_to_user((unsigned int __user *)arg, &out_count, sizeof(out_count)))
        return -EFAULT;

    return 0;
}

/**
 * _handle_kv_message - Handle ioctl commands for KV_INSERT, KV_UPDATE, KV_GET,
 * and KV_DELETE. This function copies the kv_message structure from userspace,
 * determines the type of operation based on the cmd argument, and dispatches to
 * the appropriate handler function for the specific operation. After the
 * operation is performed, it copies the result back to userspace in the same
 * kv_message structure.
 * @param cmd: The ioctl command indicating the type of operation.
 * @param arg: The argument passed from userspace, which is a pointer to a
 * kv_message structure.
 */
static int _handle_kv_message(int cmd, void __user *arg) {
    struct kv_message msg;
    int ret;

    if (copy_from_user(&msg, (struct kv_message __user *)arg, sizeof(msg)))
        return -EFAULT;

    switch (cmd) {
    case KV_INSERT:
        ret = _handle_insert(&msg);
        break;
    case KV_UPDATE:
        ret = _handle_update(&msg);
        break;
    case KV_GET:
        ret = _handle_get(&msg);
        break;
    case KV_DELETE:
        ret = _handle_delete(&msg);
        break;
    default:
        printk(KERN_WARNING "Unknown operation: %d\n", cmd);
        return -EINVAL;
    }

    if (ret < 0)
        return ret;

    if (copy_to_user((struct kv_message __user *)arg, &msg, sizeof(msg)))
        return -EFAULT;

    return ret;
}

/**
 * _handle_kv_snapshot - Handle the KV_SNAPSHOT ioctl command to get a snapshot
 * of all key-value pairs in the hash table. This function first copies the
 * kv_snapshot_message header from userspace to get the requested capacity, then
 * it verify the entry count and if it matches it calls the kv_hash_snapshot
 * function to fill in the entries. Finally, it copies the entire snapshot back
 * to userspace.
 * @param arg: The argument passed from userspace, which is a pointer to a
 * kv_snapshot_message structure. The caller (deamon.cpp) is expected to
 * allocate the amount of memory needed for the entries for this to work.
 */
static int _handle_kv_snapshot(void __user *arg) {
    struct kv_snapshot_message *snapshot_msg;
    struct kv_snapshot_message user_header;
    int actual_count;
    u32 alloc_count;
    size_t alloc_size;
    int ret;

    // First copy the header from user to get the requested capacity
    if (copy_from_user(&user_header, (struct kv_snapshot_message __user *)arg,
                       sizeof(struct kv_snapshot_message)))
        return -EFAULT;

    printk("User requested capacity: %u\n", user_header.capacity);
    // Get the actual count
    actual_count = kv_hash_count(&kv_store);
    printk("Actual count of entries in hash table: %d\n", actual_count);
    // check if the user allocated the correct capacity
    if (!(actual_count == user_header.capacity)) {
        printk("Mismatch between actual count and user requested capacity\n");
        return actual_count;
    }

    alloc_size =
        sizeof(*snapshot_msg) + (actual_count * sizeof(struct kv_message));
    snapshot_msg = kmalloc(alloc_size, GFP_KERNEL);
    if (!snapshot_msg)
        return -ENOMEM;

    snapshot_msg->capacity = actual_count;
    ret = _handle_snapshot(snapshot_msg);
    if (ret < 0) {
        kfree(snapshot_msg);
        return ret;
    }
    // Copy the snapshot back to user space
    alloc_size = sizeof(*snapshot_msg) +
                 (snapshot_msg->count * sizeof(struct kv_message));
    if (copy_to_user((struct kv_snapshot_message __user *)arg, snapshot_msg,
                     alloc_size)) {
        kfree(snapshot_msg);
        return -EFAULT;
    }
    kfree(snapshot_msg);
    return ret;
}

/**
 * _handle_insert - Handle the KV_INSERT ioctl. This function copies the key and
 * value from the kv_message structure, calls kv_hash_add to perform the
 * insertion, and updates the status and value fields in the kv_message
 * structure based on the result of the operation.
 * @param msg: Pointer to the kv_message structure.
 * @return 0 on success, negative error code on failure
 */
int _handle_insert(struct kv_message *msg)
{
    char input_value[MAX_VALUE_LEN];
    ssize_t ret;
    //buffer for checking if it already exists
    char existing[MAX_VALUE_LEN];

    memset(existing, 0, sizeof(existing));
    ret = kv_hash_lookup(&kv_store, (hash_key_t)msg->key, existing, sizeof(existing));

    // abort if it already exists
    if (ret >= 0) {
        msg->status = 1;
        snprintf(msg->value, MAX_VALUE_LEN, "Key exists. Use update.\n");
        return 0;
    }

    strscpy(input_value, msg->value, MAX_VALUE_LEN);
    ret = kv_hash_add(&kv_store, (hash_key_t)msg->key, msg->value);
    if (ret < 0) {
        msg->status = ret;
        snprintf(msg->value, MAX_VALUE_LEN, "Insert failed.\n");
        return 0;
    }

    msg->status = 0;
    return 0;
}

/**
 * _handle_delete - Handle the KV_DELETE ioctl. This function copies the key
 * from the kv_message structure, calls kv_hash_remove to perform the deletion,
 * and updates the status and value fields in the kv_message structure based on
 * the result of the operation.
 * @param msg: Pointer to the kv_message structure.
 * @return 0 on success, negative error code on failure
 */
int _handle_delete(struct kv_message *msg) {
    msg->status = 1;
    int ret = kv_hash_remove(&kv_store, (hash_key_t)msg->key);
    if (ret < 0) {
        printk("Failed to delete key\n");
        printk("Got key: %llu\n", msg->key);
        snprintf(msg->value, MAX_VALUE_LEN, "Failed to delete key.\n");
    } else {
        msg->status = 0;
    }
    return 0;
}

int _handle_get(struct kv_message *msg) {
    ssize_t ret;
    memset(msg->value, 0, MAX_VALUE_LEN);
    msg->status = 1;

    ret = kv_hash_lookup(&kv_store, (hash_key_t)msg->key, msg->value,
                         MAX_VALUE_LEN);

    if (ret < 0) {
        printk("Failed to get value for key: %llu (err=%zd)\n", msg->key, ret);
        snprintf(msg->value, MAX_VALUE_LEN, "Key '%llu' does not exist",
                 msg->key);
        printk("Key not found: %llu\n", msg->key);

    } else {
        msg->status = 0;
    }
    return 0;
}

int _handle_update(struct kv_message *msg) {
    ssize_t ret;
    char tmp[MAX_VALUE_LEN];
    msg->status = 1;
    // Check if key exists
    ret =
        kv_hash_lookup(&kv_store, (hash_key_t)msg->key, tmp, MAX_VALUE_LEN);
    if (ret < 0) {
        printk("Cannot update non-existent key: %llu\n", msg->key);
        snprintf(msg->value, MAX_VALUE_LEN,
                 "Cannot update non-existent key: %llu\n", msg->key);
        return 0;
    }

    ret = kv_hash_add(&kv_store, (hash_key_t)msg->key, msg->value);
    if (ret < 0) {
        msg->status = ret;
        snprintf(msg->value, MAX_VALUE_LEN, "Update failed.\n");
        return 0;
    }
    
    msg->status = 0;
    return 0;
}

/**
 * _handle_snapshot - Handle the KV_SNAPSHOT ioctl command to get a snapshot of
 * all key-value pairs in the hash table. This function calls kv_hash_snapshot
 * to fill in the entries in the kv_snapshot_message structure with the current
 * key-value pairs in the hash table.
 * @param msg: Pointer to the kv_snapshot_message structure that will be filled
 * with the snapshot data.
 * @return 0 on success, negative error code on failure
 */
int _handle_snapshot(struct kv_snapshot_message *msg) {
    u32 requested_capacity;
    u32 count = 0;
    bool truncated = false;
    int ret;

    if (!msg) {
        return -EINVAL;
    }

    ret = kv_hash_snapshot(&kv_store, msg->entries, msg->capacity, &count);
    if (ret < 0) {
        msg->status = ERROR;
        msg->count = 0;
        return ret;
    }

    msg->count = count;
    msg->status = SUCCESS;
    return 0;
}
