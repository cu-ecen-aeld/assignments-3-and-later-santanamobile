/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer, size_t char_offset, size_t *entry_offset_byte_rtn)
{
    size_t current_offset = 0;
    size_t index;
    
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
        // Calculate the actual index in the circular buffer
        size_t circular_index = (buffer->out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        
        // If char_offset is within the range of this entry, return it
        if (current_offset + buffer->entry[circular_index].size > char_offset) {
            *entry_offset_byte_rtn = char_offset - current_offset;
            return &buffer->entry[circular_index];
        }
        
        // Move to the next entry
        current_offset += buffer->entry[circular_index].size;
        
        // If we reach the end, stop
        if (circular_index == buffer->in_offs && !buffer->full) {
            break;
        }
    }
    
    return NULL; // Return NULL if char_offset exceeds the total buffer size
}

void aesd_circular_buffer_add_entry(
    struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Add the new entry at the current 'in' offset
    buffer->entry[buffer->in_offs] = *add_entry;
    
    // Check if buffer was full before this addition
    if (buffer->full) {
        // Move 'out' offset to next entry, as the oldest entry is overwritten
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    
    // Advance 'in' offset to the next position
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    
    // If 'in' has caught up with 'out', the buffer is now full
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }
}

void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}

