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

size_t free_space = PIPE_SIZE;

DEFINE_MUTEX(pipe_mutex);
DECLARE_WAIT_QUEUE_HEAD(writers);
DECLARE_WAIT_QUEUE_HEAD(readers);

void update_pipe(void)
{
	if (rd_ptr == wr_ptr)
		free_space = PIPE_SIZE;
	else
		free_space = (rd_ptr + PIPE_SIZE - wr_ptr) % PIPE_SIZE;
}

ssize_t misc_read(struct file *f, char __user *buf, size_t count, loff_t *f_pos)
{
	size_t retval, read;

	mutex_lock(&pipe_mutex);

	while (free_space == PIPE_SIZE) {
		mutex_unlock(&pipe_mutex);

		if (wait_event_interruptible(readers, free_space != PIPE_SIZE))
			return -ERESTART;

		mutex_lock(&pipe_mutex);
	}

	retval = min(count, PIPE_SIZE - free_space);

	if (rd_ptr < wr_ptr) {
		if (copy_to_user(buf, rd_ptr, retval))
			goto memory_fail;

		rd_ptr += retval;
	} else {
		read = min(retval, (size_t)(pipe_end - rd_ptr));
		if (copy_to_user(buf, rd_ptr, read))
			goto memory_fail;

		rd_ptr += read;
		if (rd_ptr == pipe_end)
			rd_ptr = pipe_begin;

		if (read != retval) {
			if (copy_to_user(buf + read, rd_ptr, retval - read))
				goto memory_fail;

			rd_ptr += retval - read;
		}
	}

	update_pipe();
	mutex_unlock(&pipe_mutex);
	wake_up_interruptible(&writers);

	return retval;

memory_fail:
	update_pipe();
	mutex_unlock(&pipe_mutex);
	return -EFAULT;
}

ssize_t misc_write(struct file *f, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	size_t written, left;

	if (count > PIPE_SIZE)
		return -EINVAL;

	mutex_lock(&pipe_mutex);

	while (free_space < count) {
		mutex_unlock(&pipe_mutex);

		if (wait_event_interruptible(writers, free_space >= count))
			return -ERESTART;

		mutex_lock(&pipe_mutex);
	}

	if (wr_ptr < rd_ptr) {
		if (copy_from_user(wr_ptr, buf, count))
			goto memory_fail;

		wr_ptr += count;
	} else {
		written = min(count, (size_t)(pipe_end - wr_ptr));

		if (copy_from_user(wr_ptr, buf, written))
			goto memory_fail;

		wr_ptr += written;
		if (wr_ptr == pipe_end)
			wr_ptr = pipe_begin;

		left = count - written;
		if (left) {
			if (copy_from_user(wr_ptr, buf + written, left))
				goto memory_fail;

			wr_ptr += left;
		}
	}

	update_pipe();
	mutex_unlock(&pipe_mutex);
	wake_up_interruptible(&readers);

	return count;

memory_fail:
	update_pipe();
	mutex_unlock(&pipe_mutex);
	return -EFAULT;
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
