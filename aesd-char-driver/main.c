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
#include <linux/fs.h> // file_operations
#include <linux/mutex.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

#define SUPPRESS_UNUSED_RESULT(expr) ((void)!(expr))

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Yigit Orcun Gokbulut"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file* filp, loff_t offset, int whence);
long aesd_unlocked_ioctl(struct file* filp, unsigned int , unsigned long);

int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0; 
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (down_read_interruptible(&aesd_device.sem) != 0)
    {
        return -EINTR;
    }

    size_t skip_index = *f_pos;
    size_t pos = 0;
    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        uint8_t buffer_pos = (aesd_device.buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        struct aesd_buffer_entry* current_entry = &aesd_device.buffer.entry[buffer_pos];

        if (skip_index > current_entry->size)
        {
            skip_index -= current_entry->size;
            continue;
        }
        else
        {
            size_t copy_size = current_entry->size - skip_index;
            if (copy_size > count)
                copy_size = count;

            SUPPRESS_UNUSED_RESULT(copy_to_user(buf + pos, current_entry->buffptr + skip_index, copy_size));
            
            pos += copy_size;
            skip_index = 0;

            if (pos == count)
                break;
        }
    }

    up_read(&aesd_device.sem);

    *f_pos += pos;

    return pos;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    PDEBUG("write %zu bytes with offset %lld",count, *f_pos);

    mutex_lock(&aesd_device.lineBufferMutex);

    if (aesd_device.lineBuffer == NULL)
    {
        char* newBuffer = kmalloc(count, GFP_KERNEL);
        if (newBuffer == NULL)
        {
            mutex_unlock(&aesd_device.lineBufferMutex);
            return -ENOMEM;
        }

        aesd_device.lineBufferSize = count;
        aesd_device.lineBuffer = newBuffer;
        SUPPRESS_UNUSED_RESULT(copy_from_user(aesd_device.lineBuffer, buf, count));
    }
    else
    {
        char* newBuffer = krealloc(aesd_device.lineBuffer, aesd_device.lineBufferSize + count, GFP_KERNEL);
        if (newBuffer == NULL)
        {
            mutex_unlock(&aesd_device.lineBufferMutex);
            return -ENOMEM;
        }

        SUPPRESS_UNUSED_RESULT(copy_from_user(newBuffer + aesd_device.lineBufferSize, buf, count));

        aesd_device.lineBuffer = newBuffer;
        aesd_device.lineBufferSize = aesd_device.lineBufferSize + count;
    }

    for (size_t i = aesd_device.lineBufferSize - count; i < aesd_device.lineBufferSize; i++)
    {
        if (aesd_device.lineBuffer[i] != '\n')
            continue;

        down_write(&aesd_device.sem);

        struct aesd_buffer_entry entry;
        entry.buffptr = kmalloc(i + 1, GFP_KERNEL);
        if (entry.buffptr == NULL)
        {
            up_write(&aesd_device.sem);
            mutex_unlock(&aesd_device.lineBufferMutex);
            return -ENOMEM;
        }

        memcpy((char*)entry.buffptr, aesd_device.lineBuffer, i + 1);
        entry.size = i + 1;
        
        aesd_circular_buffer_add_entry(&aesd_device.buffer, &entry);
        
        size_t totalSize = 0;
        for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
            totalSize += aesd_device.buffer.entry[i].size;
        *f_pos = totalSize;
        
        up_write(&aesd_device.sem);

        aesd_device.lineBufferSize = aesd_device.lineBufferSize - i - 1;
        memmove(aesd_device.lineBuffer, aesd_device.lineBuffer + i + 1, aesd_device.lineBufferSize);
        i = 0;
    }

    mutex_unlock(&aesd_device.lineBufferMutex);

    return count;
}

loff_t aesd_llseek(struct file* filp, loff_t offset, int whence)
{
    PDEBUG("Seek offset %lld whence %d", offset, whence);

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
        return -EINVAL;

    down_write(&aesd_device.sem);

    size_t totalSize = 0;
    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
        totalSize += aesd_device.buffer.entry[i].size;

    loff_t result = fixed_size_llseek(filp, offset, whence, totalSize);

    up_write(&aesd_device.sem);

    return result;
}

long aesd_unlocked_ioctl(struct file* filp, unsigned int command, unsigned long command_parameters)
{
    if (command != AESDCHAR_IOCSEEKTO)
        return -EINVAL;

    struct aesd_seekto params;
    SUPPRESS_UNUSED_RESULT(copy_from_user(&params, (struct aesd_seekto*)command_parameters, sizeof(struct aesd_seekto)));

    PDEBUG("Ioctl command %d, write_cmd %u, write_cmd_offset %u", command, params.write_cmd, params.write_cmd_offset);

    down_write(&aesd_device.sem);

    if (params.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        up_write(&aesd_device.sem);
        return -EINVAL;
    }

    uint8_t index = (aesd_device.buffer.out_offs + params.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (params.write_cmd_offset >= aesd_device.buffer.entry[index].size)
    {
        up_write(&aesd_device.sem);
        return -EINVAL;
    }

    size_t offset = params.write_cmd_offset;
    for (uint8_t i = 0; i < params.write_cmd; i++)
    {
        index = (aesd_device.buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        offset += aesd_device.buffer.entry[index].size;
    }

    size_t totalSize = 0;
    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
        totalSize += aesd_device.buffer.entry[i].size;

    fixed_size_llseek(filp, offset, SEEK_SET, totalSize);

    up_write(&aesd_device.sem);

    return 0;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
      
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));
    
    aesd_circular_buffer_init(&aesd_device.buffer);
    init_rwsem(&aesd_device.sem);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
