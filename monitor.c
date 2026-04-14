#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define DEVICE_NAME "container_monitor"

MODULE_LICENSE("GPL");

static int major;

struct node {
    pid_t pid;
    int warned;
    struct node *next;
};

static struct node *head = NULL;

#define SOFT 40000
#define HARD 64000

long get_rss(struct task_struct *task) {
    return get_mm_rss(task->mm) << (PAGE_SHIFT - 10);
}

void add(pid_t pid) {
    struct node *n = kmalloc(sizeof(*n), GFP_KERNEL);
    n->pid = pid;
    n->warned = 0;
    n->next = head;
    head = n;

    printk(KERN_INFO "Monitor: Added PID %d\n", pid);
}

static ssize_t write_dev(struct file *f, const char __user *buf,
                         size_t len, loff_t *off) {
    char kbuf[32];
    pid_t pid;

    copy_from_user(kbuf, buf, len);
    kbuf[len] = '\0';

    sscanf(kbuf, "%d", &pid);
    add(pid);

    return len;
}

static struct file_operations fops = {
    .write = write_dev,
};

static int thread_fn(void *data) {
    while (!kthread_should_stop()) {
        struct node *cur = head;

        while (cur) {
            struct task_struct *task;

            task = pid_task(find_vpid(cur->pid), PIDTYPE_PID);

            if (task && task->mm) {
                long rss = get_rss(task);

                if (rss > SOFT && !cur->warned) {
                    printk(KERN_WARNING "PID %d SOFT limit: %ld KB\n",
                           cur->pid, rss);
                    cur->warned = 1;
                }

                if (rss > HARD) {
                    printk(KERN_ERR "PID %d HARD limit → KILL\n",
                           cur->pid);
                    send_sig(SIGKILL, task, 0);
                }
            }

            cur = cur->next;
        }

        msleep(2000);
    }
    return 0;
}

static struct task_struct *t;

static int __init init_mod(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);

    t = kthread_run(thread_fn, NULL, "monitor");

    printk(KERN_INFO "Monitor loaded. Major: %d\n", major);
    return 0;
}

static void __exit exit_mod(void) {
    kthread_stop(t);
    unregister_chrdev(major, DEVICE_NAME);
    printk(KERN_INFO "Monitor unloaded\n");
}

module_init(init_mod);
module_exit(exit_mod);
