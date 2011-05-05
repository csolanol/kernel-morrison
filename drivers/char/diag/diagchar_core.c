/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora Forum nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * Alternatively, provided that this notice is retained in full, this software
 * may be relicensed by the recipient under the terms of the GNU General Public
 * License version 2 ("GPL") and only version 2, in which case the provisions of
 * the GPL apply INSTEAD OF those given above.  If the recipient relicenses the
 * software under the GPL, then the identification text in the MODULE_LICENSE
 * macro must be changed to reflect "GPLv2" instead of "Dual BSD/GPL".  Once a
 * recipient changes the license terms to the GPL, subsequent recipients shall
 * not relicense under alternate licensing terms, including the BSD or dual
 * BSD/GPL terms.  In addition, the following license statement immediately
 * below and between the words START and END shall also then apply when this
 * software is relicensed under the GPL:
 *
 * START
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 and only version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * END
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/diagchar.h>
#include <linux/sched.h>
#include <mach/usbdiag.h>
#include <mach/msm_smd.h>
#include <asm/current.h>
#include "diagchar_hdlc.h"
#include "diagfwd.h"
#include "diagmem.h"
#include "diagchar.h"

//#define DIAG_DEBUG	// enable debugging

MODULE_DESCRIPTION("Diag Char Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");

struct diagchar_dev *driver;

/* The following variables can be specified by module options */
static unsigned int itemsize = 2512; /*Size of item in the mempool*/
static unsigned int poolsize = 50;  /*Number of items in the mempool*/
/* This is the maximum number of user-space clients supported */
static unsigned int max_clients = 5;

module_param(itemsize, uint, 0);
module_param(poolsize, uint, 0);
module_param(max_clients, uint, 0);

static int diagchar_open(struct inode *inode, struct file *file)
{
	int i = 0;
	int *private_data;

#ifdef DIAG_DEBUG
	printk(KERN_INFO "diagchar_open\n");
#endif
	if (driver) {
		mutex_lock(&driver->diagchar_mutex);
		
		for (i = 0; i < driver->num_clients; i++)
			if (driver->client_map[i] == 0)
				break;

		if (i < driver->num_clients)
			driver->client_map[i] = current->tgid;
		else
			return -ENOMEM;

		driver->data_ready[i] |= MSG_MASKS_TYPE;
		driver->data_ready[i] |= EVENT_MASKS_TYPE;
		driver->data_ready[i] |= LOG_MASKS_TYPE;

		if (driver->ref_count == 0 && driver->count == 0)
			diagmem_init(driver);
		driver->ref_count++;
		mutex_unlock(&driver->diagchar_mutex);

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
		if ((private_data = kzalloc (sizeof(int), GFP_KERNEL)) != NULL)
			// add private data to mark TCMD clients
			file->private_data = private_data;
#endif
		return 0;
	}
	return -ENOMEM;
}

static int diagchar_close(struct inode *inode, struct file *file)
{
	int i = 0;
	int *private_data = file->private_data;

	/* Delete the pkt response table entry for the exiting process */
	for (i = 0; i < REG_TABLE_SIZE; i++)
			if (driver->table[i].process_id == current->tgid)
					driver->table[i].process_id = 0;

	if (driver) {
		mutex_lock(&driver->diagchar_mutex);
		driver->ref_count--;
		diagmem_exit(driver);
		for (i = 0; i < driver->num_clients; i++)
			if (driver->client_map[i] ==
			     current->tgid) {
#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	    	   		if (driver->tcmd_client_map[i] != 0) {
#ifdef DIAG_DEBUG
					printk(KERN_INFO "diagchar_close: TCMD client index %d exits\n", i);
#endif
	    	   			driver->tcmd_client_map[i] = 0;
					driver->data_ready[i] = 0;
				}
#endif
				driver->client_map[i] = 0;
				break;
			}
		mutex_unlock(&driver->diagchar_mutex);

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
		if (private_data != NULL)
			kfree (private_data);
#endif
		return 0;
	}
	return -ENOMEM;
}

static int diagchar_ioctl(struct inode *inode, struct file *filp,
			   unsigned int iocmd, unsigned long ioarg)
{
	int i, ret = -EINVAL, count_entries = 0;
	int *private_data = filp->private_data;
	struct bindpkt_params_per_process *pkt_params =
			 (struct bindpkt_params_per_process *) ioarg;

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	switch (iocmd) {
	case 0: /* DIAG_IOCTL_COMMAND_REG */
#endif
		for (i = 0; i < REG_TABLE_SIZE; i++) {
			if (driver->table[i].process_id == 0) {
				driver->table[i].cmd_code =
					 pkt_params->params->cmd_code;
				driver->table[i].subsys_id =
					 pkt_params->params->subsys_id;
				driver->table[i].cmd_code_lo =
					 pkt_params->params->cmd_code_hi;
				driver->table[i].cmd_code_hi =
					 pkt_params->params->cmd_code_lo;
				driver->table[i].process_id = current->tgid;
				count_entries++;
				if (pkt_params->count > count_entries)
						pkt_params->params++;
				else
						break;
			}
		}
#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
		break;
	case 1: /* DIAG_IOCTL_COMMAND_DEREG */
		printk(KERN_ERR "%s:DIAG_IOCTL_COMMAND_DEREG unhandled\n", __func__);
		break;
	case 10: /* TC_MOT_FTM_IOCTL_REQUEST */
		printk(KERN_ERR "%s: TC_MOT_FTM_IOCTL_REQUEST received\n", __func__);
		if (private_data)
			*private_data = (int)1;
		mutex_lock(&driver->diagchar_mutex);
		for (i = 0; i < REG_TABLE_SIZE; i++)
		    if (driver->client_map[i] == current->tgid) {
	    	   	driver->tcmd_client_map[i] = current->tgid;
			driver->data_ready[i] = 0;
#ifdef DIAG_DEBUG
			printk(KERN_INFO "diagchar_ioctl: TCMD client index %d comes in\n", i);
#endif
			break;
		    }
		driver->in_busy = 0;	// enable smd_read()
		driver->in_busy_qdsp = 0;
		mutex_unlock(&driver->diagchar_mutex);
		ret = 0;
		break;
	case 11: /* TC_MOT_FTM_IOCTL_RELEASE */
		printk(KERN_ERR "%s: TC_MOT_FTM_IOCTL_RELEASE received\n", __func__);
		if (private_data)
			*private_data = (int)0;
		mutex_lock(&driver->diagchar_mutex);
		for (i = 0; i < REG_TABLE_SIZE; i++)
		    if (driver->client_map[i] == current->tgid) {
	    	   	driver->tcmd_client_map[i] = 0;
			driver->data_ready[i] = 0;
#ifdef DIAG_DEBUG
			printk(KERN_INFO "diagchar_ioctl: TCMD client index %d comes out\n", i);
#endif
			break;
		    }
		mutex_unlock(&driver->diagchar_mutex);
		ret = 0;
		break;
	default:
		printk(KERN_ERR "%s: unknown ioctl\n", __func__); 
		break;
	}
#endif
	return ret;
}

static int diagchar_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	int index = -1, i = 0, ret = 0;
	int data_type;
	int tcmd_cli = file->private_data ? *(int *)(file->private_data) : 0;

	for (i = 0; i < driver->num_clients; i++)
		if (driver->client_map[i] == current->tgid)
	    	{
#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
			if (tcmd_cli && driver->tcmd_client_map[i] == 0)
				continue;
#endif
		    index = i;
		    break;
		}

	if (index == -1) {
		//printk ("diagchar_read: returning EINVAL to process pid %d\n", current->pid);
		return -EINVAL;
	}

	wait_event_interruptible(driver->wait_q,
				  driver->data_ready[index]);
	mutex_lock(&driver->diagchar_mutex);

	if (driver->data_ready[index] & MSG_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & MSG_MASKS_TYPE;
		if (copy_to_user(buf, (void *)&data_type, 4)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += 4;

		if (copy_to_user(buf+4, (void *)driver->msg_masks,
				  MSG_MASK_SIZE)) {
			ret =  -EFAULT;
			goto exit;
		}
		ret += MSG_MASK_SIZE;
		driver->data_ready[index] ^= MSG_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & EVENT_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & EVENT_MASKS_TYPE;
		if (copy_to_user(buf, (void *)&data_type, 4)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += 4;
		if (copy_to_user(buf+4, (void *)driver->event_masks,
				  EVENT_MASK_SIZE)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += EVENT_MASK_SIZE;
		driver->data_ready[index] ^= EVENT_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & LOG_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & LOG_MASKS_TYPE;
		if (copy_to_user(buf, (void *)&data_type, 4)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += 4;

		if (copy_to_user(buf+4, (void *)driver->log_masks,
				 LOG_MASK_SIZE)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += LOG_MASK_SIZE;
		driver->data_ready[index] ^= LOG_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & PKT_TYPE) {

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	    if (tcmd_cli) {
#ifdef DIAG_DEBUG
		printk(KERN_INFO "diagchar_read: TCMD client index %d\n", index);
		pretty_hex_dump ("ARM9->TCMD", driver->pkt_buf, driver->pkt_length);
#endif
		if (copy_to_user(buf, (void *)driver->pkt_buf,
				 driver->pkt_length)) {
			ret = -EFAULT;
			goto exit;
		}

		driver->data_ready[index] ^= PKT_TYPE;	// clean up data ready flag
		driver->in_busy = 0;			// unblock smd_read
		driver->in_busy_qdsp = 0;
	    } else {
#endif
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & PKT_TYPE;
		if (copy_to_user(buf, (void *)&data_type, 4)) {
			ret = -EFAULT;
			goto exit;
		}
		ret += 4;

		if (copy_to_user(buf+4, (void *)driver->pkt_buf,
				 driver->pkt_length)) {
			ret = -EFAULT;
			goto exit;
		}

		driver->data_ready[index] ^= PKT_TYPE;
#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	    }
#endif
		ret += driver->pkt_length;
		goto exit;
	}

exit:
	mutex_unlock(&driver->diagchar_mutex);
	return ret;
}

static int diagchar_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	int err, index=-1, i;
	int used, ret = 0;
	struct diag_send_desc_type send = { NULL, NULL, DIAG_STATE_START, 0 };
	struct diag_hdlc_dest_type enc = { NULL, NULL, 0 };
	void *buf_copy;
	void *buf_hdlc;
	int payload_size;
	int tcmd_cli = file->private_data ? *(int *)(file->private_data) : 0;
#ifdef DIAG_DEBUG
	int length = 0, l;
#endif

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	for (i = 0; i < driver->num_clients; i++)
		if (driver->client_map[i] == current->tgid) {
		    if (tcmd_cli && driver->tcmd_client_map[i] == 0) continue;
		    index = i;
		    break;
		}

	if (index == -1){
		//printk ("diagchar_read: returning EIO to process pid %d\n", current->pid);
		return -EIO;
	}

	if (driver->tcmd_client_map[index] != 0)
		payload_size = count;
	else {
#endif
	   if (!driver->usb_connected) {
		/*Drop the diag payload */
		return -EIO;
	   }

	   /*First 4 bytes indicate the type of payload - ignore these */
	   payload_size = count - 4;
#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	}
#endif

//	buf_copy = diagmem_alloc(driver, payload_size);
        buf_copy = diagmem_alloc(driver, payload_size, POOL_TYPE_COPY);
	if (!buf_copy) {
		driver->dropped_count++;
		return -ENOMEM;
	}

	err = copy_from_user(buf_copy, buf + count - payload_size, payload_size);
	if (err) {
		printk(KERN_INFO "diagchar : copy_from_user failed \n");
		ret = -EFAULT;
		goto fail_free_copy;
	}

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	if (tcmd_cli) {
#ifdef DIAG_DEBUG
	   printk(KERN_INFO "diagchar_write: TCMD client index %d\n", index);
	   pretty_hex_dump ("TCMD->ARM9", buf_copy, payload_size);
#endif

	   err = smd_write (driver->ch, buf_copy, payload_size);
	   if (err < 0)
			ret = -EIO;
	   else		ret = payload_size;
	   goto fail_free_copy;

	   // now return payload_size and free buf_copy

	} else {
#endif

#if 0 //def DIAG_DEBUG
	   printk(KERN_DEBUG "diagchar_write: data is --> \n");
	   for (l=0; l < payload_size; l++)
		printk(KERN_DEBUG "diagchar_write:\t %x \t", *(((unsigned char *)buf_copy)+l));
#endif

	   send.state = DIAG_STATE_START;
	   send.pkt = buf_copy;
	   send.last = (void *)(buf_copy + payload_size - 1);
	   send.terminate = 1;

	   /*Allocate a buffer for CRC + HDLC framed output to USB */
	   //buf_hdlc = diagmem_alloc(driver, payload_size + 8);
	   buf_hdlc = diagmem_alloc(driver, payload_size + 8, POOL_TYPE_HDLC);
	   if (!buf_hdlc) {
		driver->dropped_count++;
		ret = -ENOMEM;
		goto fail_free_copy;
	   }

	   enc.dest = buf_hdlc;
	   enc.dest_last = (void *)(buf_hdlc + payload_size + 7);

	   diag_hdlc_encode(&send, &enc);

	   used = (uint32_t) enc.dest - (uint32_t) buf_hdlc;

//	   diagmem_free(driver, buf_copy);
	   diagmem_free(driver, buf_copy, POOL_TYPE_HDLC);
#if 0 //def DIAG_DEBUG
	   printk(KERN_DEBUG "diagchar_write: hdlc encoded data is --> \n");
	   for (l=0; l < payload_size + 8; l++) {
		printk(KERN_DEBUG "diagchar_write:\t %x \t", *(((unsigned char *)buf_hdlc)+l));
		if (*(((unsigned char *)buf_hdlc)+l) != 0x7e)
			length++;
	   }
#endif
	   err = diag_write(buf_hdlc, used);
	   if (err) {
		/*Free the buffer right away if write failed */
		ret = -EIO;
		goto fail_free_hdlc;
	   }

	   return 0;

#if defined(CONFIG_MACH_CALGARY) || defined(CONFIG_MACH_MOT)
	}
#endif 

fail_free_hdlc:
//	diagmem_free(driver, buf_hdlc);
	diagmem_free(driver, buf_hdlc, POOL_TYPE_HDLC);
	return ret;

fail_free_copy:
//	diagmem_free(driver, buf_copy);
	diagmem_free(driver, buf_copy, POOL_TYPE_COPY);
	return ret;
}

static const struct file_operations diagcharfops = {
	.owner = THIS_MODULE,
	.read = diagchar_read,
	.write = diagchar_write,
	.ioctl = diagchar_ioctl,
	.open = diagchar_open,
	.release = diagchar_close
};

static int diagchar_setup_cdev(dev_t devno)
{

	int err;

	cdev_init(driver->cdev, &diagcharfops);

	driver->cdev->owner = THIS_MODULE;
	driver->cdev->ops = &diagcharfops;

	err = cdev_add(driver->cdev, devno, 1);

	if (err) {
		printk(KERN_INFO "diagchar cdev registration failed !\n\n");
		return -1;
	}

	driver->diagchar_class = class_create(THIS_MODULE, "diag");

	if (IS_ERR(driver->diagchar_class)) {
		printk(KERN_ERR "Error creating diagchar class.\n");
		return -1;
	}

	device_create(driver->diagchar_class, NULL, devno,
				  (void *)driver, "diag");

	return 0;

}

static int diagchar_cleanup(void)
{
	if (driver) {
		if (driver->cdev) {
			/* TODO - Check if device exists before deleting */
			device_destroy(driver->diagchar_class,
				       MKDEV(driver->major,
					     driver->minor_start));
			cdev_del(driver->cdev);
		}
		if (!IS_ERR(driver->diagchar_class))
			class_destroy(driver->diagchar_class);
		kfree(driver);
	}
	return 0;
}

static int __init diagchar_init(void)
{
	dev_t dev;
	int error;

	printk(KERN_INFO "diagfwd initializing ..\n");
	driver = kzalloc(sizeof(struct diagchar_dev) + 5, GFP_KERNEL);

	if (driver) {
		driver->itemsize = itemsize;
		driver->poolsize = poolsize;
		driver->num_clients = max_clients;
		mutex_init(&driver->diagchar_mutex);
		init_waitqueue_head(&driver->wait_q);
		diagfwd_init();

		printk(KERN_INFO "diagchar initializing ..\n");
		driver->num = 1;
		driver->name = ((void *)driver) + sizeof(struct diagchar_dev);
		strlcpy(driver->name, "diag", 4);

		/* Get major number from kernel and initialize */
		error = alloc_chrdev_region(&dev, driver->minor_start,
					    driver->num, driver->name);
		if (!error) {
			driver->major = MAJOR(dev);
			driver->minor_start = MINOR(dev);
		} else {
			printk(KERN_INFO "Major number not allocated \n");
			goto fail;
		}
		driver->cdev = cdev_alloc();
		error = diagchar_setup_cdev(dev);
		if (error)
			goto fail;
	} else {
		printk(KERN_INFO "kzalloc failed\n");
		goto fail;
	}

	printk(KERN_INFO "diagchar initialized\n");
	return 0;

fail:
	diagchar_cleanup();
	diagfwd_exit();
	return -1;

}

static void __exit diagchar_exit(void)
{
	printk(KERN_INFO "diagchar exiting ..\n");
	diagmem_exit(driver);
	diagfwd_exit();
	diagchar_cleanup();
	printk(KERN_INFO "done diagchar exit\n");
}

module_init(diagchar_init);
module_exit(diagchar_exit);
