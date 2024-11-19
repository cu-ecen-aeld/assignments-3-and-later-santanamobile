#ifndef AESDCHAR_H
#define AESDCHAR_H

#ifdef __KERNEL__
#include <linux/cdev.h>
#include "aesd-circular-buffer.h"
#else
#error "This is a kernel space header file, user-space usage is not supported."
#endif

struct aesd_dev {
    struct aesd_circular_buffer buffer;   // Circular buffer for storing write entries
    struct mutex lock;                    // Mutex for synchronizing access
    char *partial_buffer;                 // Buffer for storing incomplete writes
    size_t partial_size;                  // Size of the partial buffer
    struct cdev cdev;                     // Character device structure
};

#endif /* AESDCHAR_H */
