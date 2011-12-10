#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <asm/uaccess.h>


MODULE_LICENSE("Dual BSD/GPL");

#define MODULE_NAME "vbd"


static int major_num = 0;
module_param(major_num, int, 0);

/* The sector size of the disk  */
static int logical_block_size = 512;
module_param(logical_block_size, int, 0);

/*
 * The number of sectors in the disk
 */
static int nsectors = 1024;
module_param(nsectors, int, 0);

/*
 * The required read  and write latency that has to be simulated.
 * These paramters are configurable and can be passed while
 * loading the module
 */
static int read_latency = 0;
module_param(read_latency, int, 0);
static int write_latency = 0;
module_param(write_latency, int,0);

/*
 * Error limit for latencies. This paramter
 * is always in percentage and is configurable
 * when the module is being loaded.
 */
static int error_limit = 5;
module_param(error_limit, int, 0);

/* Kernel always talks in terms of 512  */
#define KERNEL_SECTOR_SIZE 512

/* request queue for the device */
static struct request_queue * vbd_queue;

static struct vbd_device {
  unsigned long size;
  spinlock_t lock;
  u8 * data;
  struct gendisk *gd;
  struct proc_dir_entry *procfs_file;
  u8 *procfs_data;
  u32 r_lower_limit;
  u32 r_upper_limit;
  u32 w_lower_limit;
  u32 w_upper_limit;
  unsigned long r_confd_freq;
  unsigned long r_error_freq;
  unsigned long w_confd_freq;
  unsigned long w_error_freq;
} device;

static void count_latencies(int latency, int write) {

  if(write) {

	if(latency > device.w_lower_limit && latency < device.w_upper_limit)
	  device.w_confd_freq++;
	else
	  device.w_error_freq++;
  }
  else {
	if(latency > device.r_lower_limit && latency < device.r_upper_limit)
	  device.r_confd_freq++;
	else
	  device.r_error_freq++;
  }
}



/*
 * This is the fucntion that handles all the transferring
 * of the data.
 */
static void vbd_tx(struct vbd_device * dev, sector_t sector,
				   unsigned long nsect, char * buffer, int write) {

  unsigned long offset = sector * logical_block_size;
  unsigned long nbytes = nsect * logical_block_size;
  struct timeval start_time;
  struct timeval end_time;
  struct timeval final_time;
  int operation_delay, actual_delay;
  long read_latency_usec;
  long write_latency_usec;

  if ((offset + nbytes) > dev->size) {
	printk (KERN_NOTICE "vbd: Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
  }

  /*
   * Here latency is added just before read or write operations.
   * At first we find out delay for memcpy operation and then 
   * subtract that value from the delay that has to be simulated.
   * After that udelay is used to delay by that calculated time.
   * If the calculated delay is negative that means that we are 
   * already overdue so we just skip udelay and return.
   */
  if (write) {
	do_gettimeofday(&start_time);
	memcpy(dev->data + offset, buffer, nbytes);
	do_gettimeofday(&end_time);
	operation_delay = end_time.tv_usec - start_time.tv_usec;
	write_latency_usec = write_latency*1000;
	actual_delay = (write_latency_usec - operation_delay);
	if ( actual_delay < 0) 
	{
		count_latencies(operation_delay, 1);
		return;
	}
	udelay(actual_delay);
	do_gettimeofday(&final_time);
	count_latencies(final_time.tv_usec - start_time.tv_usec,1);
	printk (KERN_NOTICE "vbd:  latency %d\n", 
			operation_delay);
	printk (KERN_NOTICE "vbd: write latency %ld\n", 
			final_time.tv_usec - start_time.tv_usec);
  }
  else {
	do_gettimeofday(&start_time);
	memcpy(buffer, dev->data + offset, nbytes);
	do_gettimeofday(&end_time);
	operation_delay = end_time.tv_usec - start_time.tv_usec;
	read_latency_usec = read_latency*1000;
	actual_delay = (read_latency_usec - operation_delay);
	if ( actual_delay < 0) 
	{
		count_latencies(operation_delay, 0);
		return;
	}
	udelay(actual_delay);
	do_gettimeofday(&final_time);
	count_latencies(final_time.tv_usec - start_time.tv_usec,0);
	printk (KERN_NOTICE "vbd: latency %d\n", 
			operation_delay);
	printk (KERN_NOTICE "vbd: read latency %ld\n", 
			final_time.tv_usec - start_time.tv_usec);
  }
}

/*
 * Service each request in the queue. If the request
 * is not a REQ_TYPE_FS type then just skip the request
 * notifying that it is skipping this request.
 */
static void vbd_request(struct request_queue * q) {
  struct request *req;
  req = blk_fetch_request(q);

  while(req != NULL) {

	/* This should not happen normally but just in case */
	if(req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
	  printk(KERN_NOTICE "Skip non fs type request\n");
	  __blk_end_request_all(req, -EIO);
	  continue;
	}

	vbd_tx(&device,blk_rq_pos(req), blk_rq_cur_sectors(req),
		   req->buffer, rq_data_dir(req));
	if(!__blk_end_request_cur(req, 0))
	  req = blk_fetch_request(q);
  }
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int vbd_getgeo(struct block_device * block_device, struct hd_geometry * geo) {
	long size;

	/* We have no real geometry, of course, so make something up. */
	size = device.size * (logical_block_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	return 0;
}

static struct block_device_operations vbd_ops = {
		.owner  = THIS_MODULE,
		.getgeo = vbd_getgeo
};

static int proc_read_vbd_stats(char *page, char **start,
			off_t off, int count, int *eof, void *data)
{

  int len = 0;
  len = sprintf(page, "r_confd_freq=%ld\n"
				"r_error_freq=%ld\n"
				"w_confd_freq=%ld\n"
				"w_error_freq=%ld\n",device.r_confd_freq,
				device.r_error_freq,device.w_confd_freq,
				device.w_error_freq);

  return len;

}
static int __init vbd_init(void) {
	
  int read_latency_usec = read_latency * 1000;
  int write_latency_usec = write_latency * 1000;
  int read_confd_limit = (read_latency_usec * error_limit) / 100;
  int write_confd_limit = (write_latency_usec * error_limit) / 100;

  /*
   * Assign the delay parameters to the device.
   * The confd_limit parameters gives the values
   * to subtract and add from to get the lower and
   * and the higher value of the confidence limit.
   */
  device.r_lower_limit = read_latency_usec - read_confd_limit;
  device.r_upper_limit = read_latency_usec + read_confd_limit;
  device.w_lower_limit = write_latency_usec - write_confd_limit;
  device.w_upper_limit = write_latency_usec + write_confd_limit;
  device.r_confd_freq = 0;
  device.r_error_freq = 0;
  device.w_confd_freq = 0;
  device.w_confd_freq = 0;


  /*
   * Allocate some memory for the device
   */
  device.size = nsectors * logical_block_size;
  spin_lock_init(&device.lock);
  device.data = vmalloc(device.size);

  /*
   * if the kernel can't allocate space to this device
   * then exit with -ENOMEM
   */
  if(device.data == NULL)
	return -ENOMEM;

  /*
   * Initialize procfs entry for vbd
   */
  device.procfs_file = create_proc_read_entry(MODULE_NAME, 0444, NULL,
	proc_read_vbd_stats, NULL);

  if(device.procfs_file == NULL)
	return -ENOMEM;

  vbd_queue = blk_init_queue(vbd_request, &device.lock);

  /* if queue is not allocated then release the device */
	if(vbd_queue == NULL)
	goto out;

/*
 * Let the kernel know the queue for this device and logical block size
 * that it operate on
 */
  blk_queue_logical_block_size(vbd_queue, logical_block_size);

  /* Register the device */
  major_num = register_blkdev(major_num, MODULE_NAME);

  /* if the device is unable to get a major number then release the device */
  if(major_num < 0) {
	printk(KERN_WARNING "vbd: unable to get a major problem\n");
	goto out;
  }

  device.gd = alloc_disk(16);
  if(!device.gd)
	goto out_unregister;

  /* Populate our device structure */
  device.gd->major = major_num;
  device.gd->first_minor = 0;
  device.gd->fops = &vbd_ops;
  device.gd->private_data = &device;
  strcpy(device.gd->disk_name, MODULE_NAME);
  set_capacity(device.gd, nsectors);
  device.gd->queue = vbd_queue;
  add_disk(device.gd);

  return 0;

 out_unregister:
  unregister_blkdev(major_num, MODULE_NAME);
 out:
  vfree(device.data);
  return -ENOMEM;

}

/*
 * Cleanup and deregister the device on rmmod
 */
static void __exit vbd_exit(void) {

  remove_proc_entry(MODULE_NAME, NULL);
  del_gendisk(device.gd);
  put_disk(device.gd);
  unregister_blkdev(major_num, MODULE_NAME);
  blk_cleanup_queue(vbd_queue);
  vfree(device.data);
}

module_init(vbd_init);
module_exit(vbd_exit);
