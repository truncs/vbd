#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/time.h>

MODULE_LICENSE("Dual BSD/GPL");


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
  * The required read  and write latency that has to be simulated .
  * This paramters are configurable and can be passed while
  * loading the module
  */
static int read_latency = 0;
module_param(read_latency, int, 0);
static int write_latency = 0;
module_param(write_latency, int,0);

/*
 *  Error limit for latencies. This paramter 
 *  is always in percentage and is configurable 
 * when the module is being loaded.
 */
static int error_limit = 10;
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
  u32 read_latency;
  u32 write_latency;
  u32 read_confd_limit;
  u32 write_confd_limit;
  u64 read_confd_freq;
  u64 read_error_freq;
  u64 write_confd_freq;
  u64 write_error_freq;
} device;


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

  if ((offset + nbytes) > dev->size) {
	printk (KERN_NOTICE "vbd: Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
  }

  if (write) {
	do_gettimeofday(&start_time);
	mdelay(write_latency);
	memcpy(dev->data + offset, buffer, nbytes);
	do_gettimeofday(&end_time);
	printk (KERN_NOTICE "vbd: write latency %ld\n",
			end_time.tv_usec - start_time.tv_usec);
  }
  else {
	do_gettimeofday(&start_time);
	mdelay(read_latency);
	memcpy(buffer, dev->data + offset, nbytes);
	do_gettimeofday(&end_time);
	printk (KERN_NOTICE "vbd: write latency %ld\n",
			end_time.tv_usec - start_time.tv_usec);
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


static int __init vbd_init(void) {
  
  /*
   *  Assign the delay parameters to the device.
   *  The confd_limit parameters give s the values
   * to subtract and add from to get the lower and 
   * and the higher value of the confidence limit.
   */
  device.read_latency = read_latency;
  device.write_latency = write_latency;
  device.read_confd_limit = (read_latency * error_limit) / 100;
  device.write_confd_limit = (write_latency * error_limit) / 100;
  device.read_confd_freq = 0;
  device.read_error_freq = 0;
  device.write_confd_freq = 0;
  device.write_error_freq = 0;

  
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

  vbd_queue = blk_init_queue(vbd_request, &device.lock);

  /* if queue is not allocated then release the device */
	if(vbd_queue == NULL)
	goto out;

/*
  *  Let the kernel know the queue for this device and logical block size
  *  that it operate on
  */
  blk_queue_logical_block_size(vbd_queue, logical_block_size);

  /* Register the device */
  major_num = register_blkdev(major_num, "vbd");

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
  strcpy(device.gd->disk_name, "vbd");
  set_capacity(device.gd, nsectors);
  device.gd->queue = vbd_queue;
  add_disk(device.gd);

  return 0;

 out_unregister:
  unregister_blkdev(major_num, "vbd");
 out:
  vfree(device.data);
  return -ENOMEM;

}

/*
 *  Cleanup and deregister the device on rmmod
 */
static void __exit vbd_exit(void) {

  del_gendisk(device.gd);
  put_disk(device.gd);
  unregister_blkdev(major_num, "vbd");
  blk_cleanup_queue(vbd_queue);
  vfree(device.data);
}

module_init(vbd_init);
module_exit(vbd_exit);
