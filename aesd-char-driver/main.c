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
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Yigit Orcun Gokbulut"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

struct aesd_file_handle
{
    char* lineBuffer;
    size_t lineBufferSize;
    struct mutex lineBufferMutex;
};

int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    
    filp->private_data = kmalloc(sizeof(struct aesd_file_handle), GFP_KERNEL);
    memset(filp->private_data, 0, sizeof(struct aesd_file_handle));

    struct aesd_file_handle* handle = (struct aesd_file_handle*)filp->private_data;
    mutex_init(&handle->lineBufferMutex);

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    struct aesd_file_handle* handle = (struct aesd_file_handle*)filp->private_data;
    if (handle->lineBuffer != NULL)
    {
        kfree(handle->lineBuffer);
        handle->lineBuffer = NULL;
        handle->lineBufferSize = 0;
        kfree(handle);
    }

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (down_read_interruptible(&aesd_device.sem) != 0)
    {
        return -EINTR;
    }

    size_t entry_offset;
    struct aesd_buffer_entry* entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.buffer, *f_pos, &entry_offset);

    if (entry == NULL)
    {
        up_read(&aesd_device.sem);
        return 0;
    }

    if (entry_offset > entry->size - entry_offset)
        retval = entry->size - entry_offset;
     
    copy_to_user(buf,  entry->buffptr + entry_offset, retval);

    up_read(&aesd_device.sem);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_file_handle* handle = (struct aesd_file_handle*)filp->private_data;

    mutex_lock(&handle->lineBufferMutex);
    PDEBUG("Mutex acc");

    if (handle->lineBuffer == NULL)
    {
        handle->lineBuffer = kmalloc(count, GFP_KERNEL);
        handle->lineBufferSize = count;
        copy_from_user(handle->lineBuffer, buf, count);
    }

    for (size_t i = handle->lineBufferSize - count; i < handle->lineBufferSize; i++)
    {
        PDEBUG("I: %zu, C: %c", i, handle->lineBuffer[i]);

        if (handle->lineBuffer[i] != '\n')
            continue;

        down_write(&aesd_device.sem);
        PDEBUG("down write");

        struct aesd_buffer_entry entry;
        entry.buffptr = kmalloc(i, GFP_KERNEL);
        if (entry.buffptr == NULL)
        {
            up_read(&aesd_device.sem);
            mutex_unlock(&handle->lineBufferMutex);
            return -ENOMEM;
        }

        memcpy((char*)entry.buffptr, handle->lineBuffer, i);
        aesd_circular_buffer_add_entry(&aesd_device.buffer, &entry);

        up_write(&aesd_device.sem);
        PDEBUG("up write");

        memmove(handle->lineBuffer, handle->lineBuffer + i, handle->lineBufferSize - i);
        i = 0;
    }
    mutex_unlock(&handle->lineBufferMutex);
    PDEBUG("Mutex releasee");

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
