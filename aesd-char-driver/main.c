#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include "aesdchar.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Helder Santana");
MODULE_AUTHOR("Helder Santana");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = &aesd_device; // for use by other operations
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    ssize_t bytes_copied = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    ssize_t bytes_copied = 0;
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (!entry) {
        retval = 0; // No data to read
        goto out;
    }

    size_t bytes_to_copy = min(count, entry->size - entry_offset);
    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT;
        goto out;
    }

    bytes_copied = bytes_to_copy;
    *f_pos += bytes_copied;
    retval = bytes_copied;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    ssize_t retval = -ENOMEM;
    size_t newline_pos;
    size_t bytes_remaining;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) {
        retval = -ENOMEM;
        goto out;
    }

    if (copy_from_user(kbuf, buf, count)) {
        retval = -EFAULT;
        goto out_free;
    }

    // Check for newline
    for (newline_pos = 0; newline_pos < count; newline_pos++) {
        if (kbuf[newline_pos] == '\n')
            break;
    }

    if (newline_pos < count) {
        // Write is complete
        size_t total_size = dev->partial_size + newline_pos + 1;
        char *complete_write = kmalloc(total_size, GFP_KERNEL);
        if (!complete_write) {
            retval = -ENOMEM;
            goto out_free;
        }

        // Combine partial data with new data
        if (dev->partial_buffer) {
            memcpy(complete_write, dev->partial_buffer, dev->partial_size);
            kfree(dev->partial_buffer);
            dev->partial_buffer = NULL;
            dev->partial_size = 0;
        }
        memcpy(complete_write + dev->partial_size, kbuf, newline_pos + 1);

        // Add to circular buffer
        struct aesd_buffer_entry new_entry = {
            .buffptr = complete_write,
            .size = total_size
        };
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

        // Free old entries if the buffer was full
        if (dev->buffer.full) {
            kfree(dev->buffer.entry[dev->buffer.out_offs].buffptr);
        }

        retval = newline_pos + 1;
    } else {
        // Append to partial buffer
        bytes_remaining = count;
        char *new_partial_buffer = kmalloc(dev->partial_size + bytes_remaining, GFP_KERNEL);
        if (!new_partial_buffer) {
            retval = -ENOMEM;
            goto out_free;
        }

        if (dev->partial_buffer) {
            memcpy(new_partial_buffer, dev->partial_buffer, dev->partial_size);
            kfree(dev->partial_buffer);
        }
        memcpy(new_partial_buffer + dev->partial_size, kbuf, bytes_remaining);

        dev->partial_buffer = new_partial_buffer;
        dev->partial_size += bytes_remaining;

        retval = bytes_remaining;
    }

out_free:
    kfree(kbuf);
out:
    mutex_unlock(&dev->lock);
    return retval;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if (result) {
    if (result) {
        unregister_chrdev_region(dev, 1);
    }


    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // Free all allocated buffers in the circular buffer
    struct aesd_buffer_entry *entry;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr)
            kfree(entry->buffptr);
    }
    // Free all allocated buffers in the circular buffer
    struct aesd_buffer_entry *entry;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr)
            kfree(entry->buffptr);
    }

    unregister_chrdev_region(devno, 1);
    mutex_destroy(&aesd_device.lock);
    mutex_destroy(&aesd_device.lock);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

