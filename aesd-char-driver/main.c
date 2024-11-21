/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h> // for copy_to_user and copy_from_user

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("santanamobile");
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
{
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

    bytes_copied = entry->size - entry_offset;

    if (bytes_copied > count)
        bytes_copied = count;

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_copied)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_copied;
    retval = bytes_copied;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    char *kernel_buffer = NULL;
    // const char *entry_to_free = NULL;
    size_t newline_offset;
    struct aesd_dev *dev = filp->private_data;

    if (!dev)
        return -EFAULT;

    // Allocate temporary kernel buffer for incoming write data
    kernel_buffer = kmalloc(count, GFP_KERNEL);
    if (!kernel_buffer)
        return -ENOMEM;

    // Copy data from user-space to kernel-space
    if (copy_from_user(kernel_buffer, buf, count)) {
        retval = -EFAULT;
        goto out_free;
    }

    // Lock the device for thread-safe access
    if (mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        goto out_free;
    }

    // Handle case where newline is found in the incoming data
    for (newline_offset = 0; newline_offset < count; newline_offset++) {
        dev->partial_buffer[dev->partial_size++] = kernel_buffer[newline_offset];
        if (kernel_buffer[newline_offset] == '\n') {
            struct aesd_buffer_entry new_entry = {
                .buffptr = dev->partial_buffer,
                .size = dev->partial_size,
            };
            aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
            // entry_to_free = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
            // if (entry_to_free)
                // kfree(entry_to_free);

            dev->partial_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
            if (!dev->partial_buffer) {
                retval = -ENOMEM;
                goto unlock;
            }
            dev->partial_size = 0;
        }
    }

    retval = count;

unlock:
    mutex_unlock(&dev->lock);

out_free:
    if (kernel_buffer)
        kfree(kernel_buffer);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    loff_t new_pos = 0;
    int i = 0;
    struct aesd_dev *dev = filp->private_data;

    if (!dev) {
        return -EINVAL; // Invalid argument
    }

    // Use mutex to protect access
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS; // Interrupted by signal
    }

    // Calculate the new position based on the requested seek operation
    switch (whence) {
    case SEEK_SET:
        new_pos = off;
        break;
    case SEEK_CUR:
        new_pos = filp->f_pos + off;
        break;
    case SEEK_END:
        new_pos = 0; // Start at the end of the buffer
        for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
            size_t idx = (dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            if (idx == dev->buffer.in_offs && !dev->buffer.full) {
                break; // Stop at the last valid entry
            }
            new_pos += dev->buffer.entry[idx].size;
        }
        new_pos += off;
        break;
    default:
        mutex_unlock(&dev->lock);
        return -EINVAL; // Invalid argument
    }

    // Ensure the new position is within bounds
    if (new_pos < 0 || new_pos > LLONG_MAX) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos; // Update the file position
    mutex_unlock(&dev->lock);

    return new_pos;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_ERR "Error %d adding aesd cdev", err);

    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");

    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    aesd_device.partial_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);

    if (!aesd_device.partial_buffer)
        result = -ENOMEM;

    result = aesd_setup_cdev(&aesd_device);

    if (result)
        unregister_chrdev_region(dev, 1);

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

    kfree(aesd_device.partial_buffer);
    unregister_chrdev_region(devno, 1);
    mutex_destroy(&aesd_device.lock);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
