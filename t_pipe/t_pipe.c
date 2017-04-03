#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

//#define PIPE_SIZE PAGE_SIZE
#define PIPE_SIZE 15

char pipe_buffer[PIPE_SIZE];

char * const pipe_begin = &pipe_buffer[0];
char * const pipe_end = &pipe_buffer[PIPE_SIZE];

char *rd_ptr = pipe_buffer;
char *wr_ptr = pipe_buffer;

size_t free_space = PIPE_SIZE - 1;
#define data_len (PIPE_SIZE - free_space - 1)

DEFINE_MUTEX(pipe_mutex);
DECLARE_WAIT_QUEUE_HEAD(writers);
DECLARE_WAIT_QUEUE_HEAD(readers);

void update_pipe(void)
{
	if (rd_ptr == wr_ptr)
		free_space = PIPE_SIZE - 1;
	else
		free_space = (rd_ptr + PIPE_SIZE - wr_ptr) % PIPE_SIZE - 1;
}

ssize_t misc_read(struct file *f, char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	size_t read_len;

	mutex_lock(&pipe_mutex);

	while (data_len == 0) {
		mutex_unlock(&pipe_mutex);

		if (wait_event_interruptible(readers, (data_len != 0)))
			return -ERESTARTSYS;

		mutex_lock(&pipe_mutex);
	}

	count = min(count, data_len);
	while (count) {
		if (rd_ptr < wr_ptr)
			read_len = count;
		else
			read_len = min(count, (size_t)(pipe_end - rd_ptr));

		if (copy_to_user(buf + retval, rd_ptr, read_len)) {
			retval = -EFAULT;
			break;
		}

		count -= read_len;
		retval += read_len;
		rd_ptr += read_len;
		if (rd_ptr == pipe_end)
			rd_ptr = pipe_begin;
	}

	update_pipe();
	mutex_unlock(&pipe_mutex);
	wake_up_interruptible(&writers);

	return retval;
}

ssize_t misc_write(struct file *f, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	ssize_t retval = 0;
	size_t write_len;

	if (count > (PIPE_SIZE - 1))
		return -EINVAL;

	mutex_lock(&pipe_mutex);

	while (free_space < count) {
		mutex_unlock(&pipe_mutex);

		if (wait_event_interruptible(writers, free_space >= count))
			return -ERESTARTSYS;

		mutex_lock(&pipe_mutex);
	}

	while (count) {
		if (wr_ptr < rd_ptr)
			write_len = count;
		else
			write_len = min(count, (size_t)(pipe_end - wr_ptr));

		if (copy_from_user(wr_ptr, buf + retval, write_len)) {
			retval = -EFAULT;
			break;
		}

		retval += write_len;
		count -= write_len;
		wr_ptr += write_len;
		if (wr_ptr == pipe_end)
			wr_ptr = pipe_begin;
	}

	update_pipe();
	mutex_unlock(&pipe_mutex);
	wake_up_interruptible(&readers);

	return retval;
}

const struct file_operations misc_fops = {
	.owner = THIS_MODULE,
	.read = misc_read,
	.write = misc_write,
	.llseek = no_llseek,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "t_pipe",
	.fops = &misc_fops,
	.mode = 0666,
};

module_misc_device(misc_dev);
