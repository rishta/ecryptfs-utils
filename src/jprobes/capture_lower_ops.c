/**
 * Capture statistics from read and writes; export them to /dev/ecryptfs
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/bit_spinlock.h>
#include <linux/sched.h>

struct jprobe_mapping_elem {
	struct jprobe *jp;
	char *symbol;
	void *fp;
};

#define MAX_CLO_MSGS 32768

spinlock_t clo_msg_list_spinlock;
size_t num_clo_msgs = 0;

struct clo_msg;

struct clo_msg {
	struct clo_msg *next;
	char *msg;
	size_t size;
	size_t current_read_offset;
};

struct clo_msg *tail_clo_msg = NULL;
struct clo_msg *head_clo_msg = NULL;

int jp_do_umount(struct vfsmount *mnt, int flags)
{
	printk(KERN_INFO "%s: mnt = [0x%p]; flags = [0x%x]\n", __FUNCTION__,
	       mnt, flags);
	jprobe_return();
	return 0;
}

void jp_ecryptfs_kill_block_super(struct super_block *sb)
{
	printk(KERN_INFO "%s: sb = [0x%p]\n", __FUNCTION__, sb);
	jprobe_return();
}

void jp_ecryptfs_put_super(struct super_block *sb)
{
	printk(KERN_INFO "%s: sb = [0x%p]\n", __FUNCTION__, sb);
	jprobe_return();
}

void jp_ecryptfs_umount_begin(struct vfsmount *vfsmnt, int flags)
{
	printk(KERN_INFO "%s: vfsmnt = [0x%p]; flags = [0x%.8x]\n",
	       __FUNCTION__, vfsmnt, flags);
	jprobe_return();
}

int jp_ecryptfs_get_sb(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *raw_data,
		       struct vfsmount *mnt)
{
	printk(KERN_INFO "%s: fs_type = [0x%p]; flags = [0x%.8x], dev_name = "
	       "[%s], raw_data = [0x%p], mnt = [0x%p]\n",
	       __FUNCTION__, fs_type, flags, dev_name, raw_data, mnt);
	jprobe_return();
	return 0;
}

int jp_get_sb_nodev(struct file_system_type *fs_type,
		    int flags, void *data,
		    int (*fill_super)(struct super_block *, void *, int),
		    struct vfsmount *mnt)
{
	printk(KERN_INFO "%s: fs_type = [0x%p]; flags = [0x%.8x], data = "
	       "[0x%p], mnt = [0x%p]\n",
	       __FUNCTION__, fs_type, flags, data, mnt);
	jprobe_return();
	return 0;
}

int jp_ecryptfs_fill_super(struct super_block *sb, void *raw_data, int silent)
{
	printk(KERN_INFO "%s: sb = [0x%p]; raw_data = [0x%p], silent = [%d]\n",
	       __FUNCTION__, sb, raw_data, silent);
	jprobe_return();
	return 0;
}

int jp_ecryptfs_read_super(struct super_block *sb, const char *dev_name)
{
	printk(KERN_INFO "%s: sb = [0x%p]; dev_name = [%s]\n",
	       __FUNCTION__, sb, dev_name);
	jprobe_return();
	return 0;
}

int jp_ecryptfs_interpose(struct dentry *lower_dentry, struct dentry *dentry,
			  struct super_block *sb, int flag)
{
	printk(KERN_INFO "%s: lower_dentry = [0x%p]; dentry = [0x%p]; sb = "
	       "[0x%p]; flag = [0x%.8x]\n", __FUNCTION__, lower_dentry, dentry,
	       sb, flag);
	if (flag)
		printk(KERN_INFO "%s: d_add() will be called\n", __FUNCTION__);
	else
		printk(KERN_INFO "%s: d_instantiate() will be called\n",
		       __FUNCTION__);
	jprobe_return();
	return 0;
}

asmlinkage long jp_sys_umount(char __user * name, int flags)
{
	char *tmp = getname(name);

	if (!IS_ERR(tmp)) {
		printk(KERN_INFO "%s: name = [%s]; flags = [0x%x]\n",
		       __FUNCTION__, tmp, flags);
		putname(tmp);
	} else 
		printk(KERN_INFO "%s: (getname failed); flags = [0x%x]\n",
		       __FUNCTION__, flags);
	jprobe_return();
	return 0;	
}

/**
 * Copies msg; callee must deallocate msg, and can do so immediately
 * upon return.
 */
static int queue_msg(char *msg)
{
	ssize_t rc;

	spin_lock(&clo_msg_list_spinlock);
	if (num_clo_msgs > MAX_CLO_MSGS) {
		rc = -EBUSY;
		goto out;
	}
	if (!tail_clo_msg) {
		tail_clo_msg = head_clo_msg = kmalloc(sizeof(struct clo_msg),
						      GFP_KERNEL);
		if (!head_clo_msg) {
			rc = -ENOMEM;
			goto out;
		}
	} else {
		tail_clo_msg->next = kmalloc(sizeof(struct clo_msg),
					     GFP_KERNEL);
		if (!tail_clo_msg->next) {
			rc = -ENOMEM;
			goto out;
		}
		tail_clo_msg = tail_clo_msg->next;
	}
	memset(tail_clo_msg, 0, sizeof(*tail_clo_msg));
	tail_clo_msg->size = strlen(msg) + 1;
	tail_clo_msg->msg = kmalloc(tail_clo_msg->size, GFP_KERNEL);
	memcpy(tail_clo_msg->msg, msg, tail_clo_msg->size);
	num_clo_msgs++;
out:
	spin_unlock(&clo_msg_list_spinlock);
	return rc;
}

atomic_t writeno;

ssize_t jp_vfs_write(struct file *file, const char __user *buf, size_t count,
		     loff_t *pos)
{
	jprobe_return();
	return 0;
}

int jp_ecryptfs_write_lower(struct inode *ecryptfs_inode, char *data,
			    loff_t offset, size_t size)
{
	char tmp;
	char *msg;
	size_t sz;
	struct timespec ts = CURRENT_TIME;
	size_t writeno_tmp = atomic_read(&writeno);
	struct task_struct *task = current;
	char *task_command;

	task_command = kmalloc(sizeof(task->comm), GFP_KERNEL);
	if (!task_command) {
		printk(KERN_WARNING "%s: Out of memory\n", __FUNCTION__);
		goto out;
	}
        task_lock(task);
        strncpy(task_command, task->comm, sizeof(task->comm));
        task_unlock(task);
	atomic_inc(&writeno);
	sz = (snprintf(&tmp, 0,
		       "\"write\",\"%Zd\",\"%s\",\"%lld\",\"%Zd\","
		       "\"%ld\",\"yyyy-mm-dd hh:mm:ss.sss\"\n",
		       writeno_tmp, task_command, offset, size, ts.tv_sec) + 1);
	msg = kmalloc(sz, GFP_KERNEL);
	if (!msg)
		goto out;
	sz = (snprintf(msg, sz,
		       "\"write\",\"%Zd\",\"%s\",\"%lld\",\"%Zd\","
		       "\"%ld\",\"yyyy-mm-dd hh:mm:ss.sss\"\n",
		       writeno_tmp, task_command, offset, size, ts.tv_sec) + 1);
	queue_msg(msg);
	kfree(msg);
out:
	jprobe_return();
	return 0;
}

static ssize_t ecryptfs_read(struct file *filp, char __user *buf, size_t len,
			     loff_t *ppos);
static int ecryptfs_open(struct inode *inode, struct file *file);
static int ecryptfs_release(struct inode *inode, struct file *file);

struct file_operations ecryptfs_fops = {
	.read = ecryptfs_read,
	.open = ecryptfs_open,
	.release = ecryptfs_release,
};

static ssize_t ecryptfs_read(struct file *filp, char __user *buf, size_t len,
			     loff_t *ppos)
{
	ssize_t rc = 0;

	spin_lock(&clo_msg_list_spinlock);
	if (!head_clo_msg)
		goto out;
	if (len >= head_clo_msg->size) {
		if (head_clo_msg == tail_clo_msg)
			tail_clo_msg = NULL;
		memcpy(buf, head_clo_msg->msg, head_clo_msg->size);
		rc = head_clo_msg->size;
		kfree(head_clo_msg->msg);
	        head_clo_msg = head_clo_msg->next;
		num_clo_msgs--;
	}
out:
	spin_unlock(&clo_msg_list_spinlock);
	return rc;
}

static int ecryptfs_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ecryptfs_release(struct inode *inode, struct file *file)
{
	return 0;
}

struct jprobe_mapping_elem jprobe_mapping[] = {
	{NULL, "ecryptfs_write_lower", jp_ecryptfs_write_lower},
};

int major;
int minor;
#define ECRYPTFS_DEVICE_NAME "ecryptfs"

static int __init jprobe_mount_init(void)
{
	int i;
	int rc;

	for (i = 0; i < ARRAY_SIZE(jprobe_mapping); i++) {
		jprobe_mapping[i].jp = kzalloc(sizeof(struct jprobe),
					       GFP_KERNEL);
		jprobe_mapping[i].jp->entry = jprobe_mapping[i].fp;
		jprobe_mapping[i].jp->kp.symbol_name = jprobe_mapping[i].symbol;
		printk(KERN_INFO "%s: Registering jprobe for symbol [%s]\n",
		       __FUNCTION__, jprobe_mapping[i].symbol);
		rc = register_jprobe(jprobe_mapping[i].jp);
		if (rc < 0) {
			int j;

			printk(KERN_NOTICE "Unable to register symbol [%s]\n",
			       jprobe_mapping[i].symbol);
			for (j = 0; j < i; j++) {
				unregister_jprobe(jprobe_mapping[j].jp);
				kfree(jprobe_mapping[j].jp);
			}
			return -EINVAL;
		}
	}
	rc = register_chrdev(0, ECRYPTFS_DEVICE_NAME, &ecryptfs_fops);
	if (rc < 0) {
		printk(KERN_ERR
		       "%s: Error registering chrdev [%s]; rc = [%d]\n",
		       __FUNCTION__, ECRYPTFS_DEVICE_NAME, rc);
		major = -1;
	} else {
		major = rc;
		printk(KERN_INFO "%s: Registered major device [%d]\n",
		       __FUNCTION__, major);
	}
	spin_lock_init(&clo_msg_list_spinlock);
	atomic_set(&writeno, 0);
        return 0;
}

static void __exit jprobe_mount_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(jprobe_mapping); i++) {
		unregister_jprobe(jprobe_mapping[i].jp);
		kfree(jprobe_mapping[i].jp);
	}
	if (major >= 0) {
		unregister_chrdev(major, ECRYPTFS_DEVICE_NAME);
	} else {
		printk(KERN_WARNING "%s: Not unregistering device, since there "
		       "was an error during registration\n", __FUNCTION__);
	}
	spin_lock(&clo_msg_list_spinlock);
	while (head_clo_msg) {
		struct clo_msg *tmp;

		tmp = head_clo_msg;
		head_clo_msg = head_clo_msg->next;
		kfree(tmp);
	}
	spin_unlock(&clo_msg_list_spinlock);
}

module_init(jprobe_mount_init);
module_exit(jprobe_mount_exit);
MODULE_LICENSE("GPL");
