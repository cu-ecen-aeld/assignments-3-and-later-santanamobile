#ifndef AESDCHAR_H
#define AESDCHAR_H

#include "aesd-circular-buffer.h"

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev {
    struct aesd_circular_buffer buffer;   // Circular buffer for storing write entries
    struct mutex lock;                    // Mutex for synchronizing access
    char *partial_buffer;                 // Buffer for storing incomplete writes
    size_t partial_size;                  // Size of the partial buffer
    struct cdev cdev;                     // Character device structure
};

#endif /* AESDCHAR_H */
