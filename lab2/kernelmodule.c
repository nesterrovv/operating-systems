#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include "kernelmodule.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Nesterov");
MODULE_DESCRIPTION("Kernel module for getting vm_area_struct and net_device structs information via debugfs driver");
MODULE_VERSION("1.0");

/* debugfs directory and files */
static struct dentry *debugfs_dir;
static struct dentry *debugfs_vma_file;
static struct dentry *debugfs_vma_arg_file;
static struct dentry *debugfs_nds_file;

/* needed structs and arguments */
static struct vm_area_struct *vma;
static struct net_device *dev;
//static struct net_device *dev2;
static pid_t pid_num;

static struct mutex lock_vma;
static struct mutex lock_nds;

static char buffer_k[BUFFER_SIZE] = {NULL};
static bool init_error = false;

/* function for getting task_struct */
static struct task_struct* get_ts(pid_t nr) {
    return get_pid_task(find_get_pid(nr), PIDTYPE_PID);
}

/* function for getting vm_area_struct */
static struct vm_area_struct* get_vma(struct task_struct *ts) {
    struct mm_struct* mm = get_task_mm(ts);
    struct vm_area_struct* vma = mm -> mmap;
    return vma;
}


/* utility function for sending message to user */
static void to_user(const char __user *buff, size_t count, loff_t *fpos) {
    count = strlen(buffer_k) + 1;
    copy_to_user(buff, buffer_k, count);
    *fpos += count;
}

static int safe_open_vma(struct inode* inode, struct file* file) {
    mutex_init(&lock_vma);
    mutex_lock(&lock_vma);
    return 0;
}

static int safe_close_vma(struct inode* inode, struct file* file) {
    mutex_unlock(&lock_vma);
    return 0;
}

static int safe_open_nds(struct inode* inode, struct file* file) {
    mutex_init(&lock_nds);
    mutex_lock(&lock_nds);
    return 0;
}

static int safe_close_nds(struct inode* inode, struct file* file) {
    mutex_unlock(&lock_vma);
    return 0;
}

/* overrode function to send vm_area_struct to user when he tries to read debugfs file */
static ssize_t vma_to_user(struct file* filp, char __user* buff, size_t count, loff_t* fpos) {
    if (*fpos > 0)  {
        return -EFAULT;
    }
    if (init_error) {
        sprintf(buffer_k, KERNEL_ERROR_MSG);
    } else {
        printk(KERN_INFO "Trying to get vm_area_struct...\n");
        struct task_struct* ts = get_ts(pid_num);
        if (ts == NULL) {
            printk(KERN_WARNING "Wrong PID\n");
            sprintf(buffer_k, KERNEL_ERROR_MSG);
        } else {
            vma = get_vma(ts);
            if (vma == NULL) {
                printk(KERN_WARNING "Cannot get vm_area_struct\n");
                sprintf(buffer_k, KERNEL_ERROR_MSG);
            } else {
                char vma_to_user[10000];
                int counter = 1;
                while (vma) {
                    char current_vma_info[200];
                    char filename[50];
                    if (vma->vm_file) {
                        strcpy(filename, vma->vm_file->f_path.dentry->d_iname);
                    } else {
                        strcpy(filename, "[ anon ]");
                    }
                    sprintf(current_vma_info, "%#lx - %#lx, flags = %lu, pgoff = %lu, mapped file: %s\n",
                    vma -> vm_start, vma -> vm_end, vma -> vm_flags, vma -> vm_pgoff, filename);
                    strcat(vma_to_user, current_vma_info);
                        vma = vma->vm_next;
                    counter++;
                
                }
                sprintf(buffer_k, vma_to_user);
            } 
        }
        
    }
    to_user(buff, count, fpos);
    return *fpos;
}

/* overrode function to get PID from user when he tries to write it to debugfs file */
static ssize_t pid_from_user(struct file* filp, const char __user* buff, size_t count, loff_t* fpos) {
    if (BUFFER_SIZE < count) {
        printk(KERN_WARNING "Buffer size is not enough\n");
        sprintf(buffer_k, KERNEL_ERROR_MSG);
        to_user(buff, count, fpos);
        return -EFAULT;
    }
    if (*fpos > 0)  {
        return -EFAULT;
    }
    printk(KERN_INFO "Trying to get PID from user...\n");
    copy_from_user(buffer_k, buff, count);
    if (kstrtoint(buffer_k, 10, &pid_num) != 0) {
        printk(KERN_WARNING "PID is not decimal\n");
        return -EFAULT;
    }
    if (sscanf(buffer_k, "%d", &pid_num) < 1) {
    	printk(KERN_WARNING "Cannot read PID properly\n");
    	return -EFAULT;
    }
    *fpos = strlen(buffer_k);
    return *fpos;
}

/* overrode function to send net_device struct to user when he tries to read debugfs file */
static ssize_t nds_to_user(struct file* filp, char __user* buff, size_t count, loff_t* fpos ) {
    if (*fpos > 0) {
        return -EFAULT;
    }
    if (init_error) {
        sprintf(buffer_k, KERNEL_ERROR_MSG);
    } else {
   	printk(KERN_INFO "Trying to get net_device struct...\n");
        read_lock(&dev_base_lock);
        printk(KERN_INFO "Reading...\n");
        dev = first_net_device(&init_net);
        if (dev == NULL) {
            printk(KERN_WARNING "Cannot get net_device struct\n");
            sprintf(buffer_k, KERNEL_ERROR_MSG);
        } else {
            char data_for_user[5000];
            while (dev) {
                printk(KERN_INFO "found [%s]\n", dev->name);
                printk(KERN_INFO "MAC address: %pMF\n", dev -> dev_addr);
                printk(KERN_INFO "Broadcast address: %pMF\n", dev -> broadcast);
                char nds_info[1000];
                sprintf(nds_info, "Found network device:\n\tname: %s\n\tmem_start: %#lx\n\tmem_end: %#lx\n\tbase_addr: %#lx\n\tirq: %d\n\tstate: %lu\n\tMAC address: %pMF\n\tbroadcast address: %pMF\n\tflags: %d\n\tmtu: %d\n\tmin_mtu: %d\n\tmax_mtu: %d\n\ttype: %hu\n\tmin_header_len: %d\n\tname_assign_type: %d\n\tgroup: %d\n\tneeded_headroom: %hu\n\tneeded_tailroom: %hu\n", dev->name, dev->mem_start, dev->mem_end, dev->base_addr, dev->irq, dev->state, dev->dev_addr, dev->broadcast, dev->flags, dev->mtu, dev->min_mtu, dev->max_mtu, dev->type, dev->min_header_len, dev->name_assign_type, dev->group, dev->needed_headroom, dev->needed_tailroom);
                strcat(data_for_user, nds_info);
                dev = next_net_device(dev);
            }
            //sprintf(buffer_k, "mnt_flags = %d\n", vfs->mnt_flags);
            sprintf(buffer_k, data_for_user);
        }
        read_unlock(&dev_base_lock);
    }
    to_user(buff, count, fpos);
  
    return *fpos;
}

/* overriding read and write functions for debugfs files */
static const struct file_operations vma_file_op = {
    .open = safe_open_vma,
    .release = safe_close_vma,
    .read = vma_to_user,
    .write = pid_from_user
};

static const struct file_operations nds_file_op = {
      .open = safe_open_nds,
      .read = nds_to_user,
      .release = safe_close_nds
};

/* module initialization */
static int __init kernel_module_init(void) {
    printk(KERN_INFO "Initializing kernel module...\n");
    debugfs_dir = debugfs_create_dir(DEBUGFS_DIR_NAME, NULL);
    if (!debugfs_dir) {
        printk(KERN_WARNING "Cannot create debugfs directory\n");
        init_error = true;
        return -1;
    }
    debugfs_vma_file = debugfs_create_file(DEBUGFS_VMA_FILE_NAME, 0666, debugfs_dir, NULL, &vma_file_op);
    if (!debugfs_vma_file) {
        printk(KERN_WARNING "Cannot create debugfs file\n");
        init_error = true;
        return -1;
    }
    debugfs_vma_arg_file = debugfs_create_file(DEBUGFS_VMA_ARG_FILE_NAME, 0666, debugfs_dir, NULL, &vma_file_op);
    if (!debugfs_vma_arg_file) {
        printk(KERN_WARNING "Cannot create debugfs arg file\n");
        init_error = true;
        return -1;
    }
    debugfs_nds_file = debugfs_create_file(DEBUGFS_NDS_FILE_NAME, 0666, debugfs_dir, NULL, &nds_file_op);
    if (!debugfs_nds_file) {
        printk(KERN_WARNING "Cannot create debugfs file\n");
        init_error = true;
        return -1;
    }
    printk(KERN_INFO "Kernel module initialized successfully\n");
    return 0;
}

/* module cleanup */
static void __exit kernel_module_exit( void ) {
    debugfs_remove(debugfs_vma_file);
    debugfs_remove(debugfs_vma_arg_file);
    debugfs_remove(debugfs_nds_file);
    debugfs_remove(debugfs_dir);
}

/* registering module init and exit functions */
module_init(kernel_module_init);
module_exit(kernel_module_exit);

