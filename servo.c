/*
 *		Stepper motor driver with pwm emulating over gpio
 *
 *		Copyright (R) 2010-2011 Claudio Mignanti - <c.mignanti@gmail.com>
 *
 *		This program is free software; you can redistribute it and/or modify
 *		it under the terms of the GNU General Public License as published by
 *		the Free Software Foundation.
 *
 * ---------------------------------------------------------------------------
*/

#include <linux/init.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>

#include <asm/uaccess.h>

#include "servo.h"

#define DRV_NAME	"servo"
#define DRV_DESC	"Servo motor driver using gpio and pwm pins"
#define DRV_VERSION	"0.1"

#define MAX_MOT_NUM 4

/* module var*/
struct class *motor_class;
struct motor_device *motor;
static dev_t motor_devno = 0;

struct motor_device {
	int enable;
	int degree;

	struct hrtimer hrt;
	ktime_t interval;

	struct cdev *mcdev;
	struct fasync_struct *fasync;
};

static unsigned int mot0[1] __initdata;
static unsigned int mot1[1] __initdata;
static unsigned int mot2[1] __initdata;
static unsigned int mot3[1] __initdata;

static unsigned int mot_nump[MAX_MOT_NUM] __initdata;

static int min_ns;

#define BUS_PARM_DESC \
	" config -> pin"

module_param_array(mot0, uint, &mot_nump[0], 0);
MODULE_PARM_DESC(mot0, "mot0" BUS_PARM_DESC);
module_param_array(mot1, uint, &mot_nump[1], 0);
MODULE_PARM_DESC(mot1, "mot1" BUS_PARM_DESC);
module_param_array(mot2, uint, &mot_nump[2], 0);
MODULE_PARM_DESC(mot2, "mot2" BUS_PARM_DESC);
module_param_array(mot3, uint, &mot_nump[3], 0);
MODULE_PARM_DESC(mot3, "mot3" BUS_PARM_DESC);


struct motor_device * find_hrt (struct hrtimer *t)
{
	int i;
	for ( i=0; i < MAX_MOT_NUM ; i++ ) {
		if ( &(motor[i].hrt) == t)
			return &(motor[i]);
	}
	return 0;
}

struct motor_device * find_cdev (struct cdev *cdev)
{
	int i;
	for ( i=0; i < MAX_MOT_NUM ; i++ ) {
		if ( motor[i].mcdev == cdev)
			return &(motor[i]);
	}
	return 0;
}

static irqreturn_t stepper_irq(int irq, void *motor){
	return IRQ_HANDLED;
}

static enum hrtimer_restart gpio_timeout(struct hrtimer *t)
{
	/* FIXME TODO */
	struct motor_device *mot = find_hrt(t);

	if ((mot->count && mot->steps >= mot->steps_max) || mot->cancel ) {
		hrtimer_try_to_cancel(&(mot->hrt));
		kill_fasync(&mot->fasync, SIGIO, POLL_IN);

		return HRTIMER_NORESTART;
	}

	if (mot->status) {
		gpio_set_value(mot->g_step ,0);
		mot->status = 0;
	} else {
		gpio_set_value(mot->g_step ,1);
		mot->status = 1;
		mot->steps++;
	}

	hrtimer_forward(&(mot->hrt), ktime_get(), mot->interval);
	return HRTIMER_RESTART;
}

/* IOCTL interface */
long motor_ioctl (struct file *file, unsigned int cmd, unsigned long arg){

	int retval = 0;
	unsigned long to_end;

	struct motor_device *motor = file->private_data;

	printk(KERN_INFO "servo: ioctl if  command %d  args %ld \n", cmd, arg);

	switch (cmd) {
		case 1:		//enable
			if ((int)arg)
				gpio_set_value (motor->g_enable, 0);
			else
				gpio_set_value (motor->g_enable, 1);
			break;

		case MOTOR_START:
			motor->count = 1;	/* execute step_max steps */
			motor->cancel = 0;
			if ((int)arg != 0) {
				motor->steps_max = (int)arg;
				motor->steps=0;
			}
			hrtimer_start(&(motor->hrt), motor->interval, HRTIMER_MODE_REL);
			break;

		case MOTOR_PWM_ON:
			/* run without step count */
			motor->count = 0;
			motor->cancel = 0;
			hrtimer_start(&(motor->hrt), motor->interval, HRTIMER_MODE_REL);
			break;

		case MOTOR_PWM_OFF:
			hrtimer_cancel(&(motor->hrt));
			break;

		case MOTOR_PWM_SET:
			/* set the pwm period in ns */
			if ((long)arg < min_ns) {
				printk(KERN_INFO "stepper: Error ioctl MOTOR_PWM_SET is smaller that min_ns.\n");
				return -1;
			}
			motor->interval = ktime_set(0, (long)arg);
			break;

		case MOTOR_RESET:
			motor->steps = 0; /* set the actual position as home */
			break;

		case MOTOR_LOWPWR:
			if ((int)arg)
				gpio_set_value (motor->g_lpwr, 1);
			else
				gpio_set_value (motor->g_lpwr, 0);
			break;

		/* return steps_max-step */
		case MOTOR_TO_END:
			to_end = motor->steps_max - motor->steps;
			copy_to_user(&arg, &to_end, sizeof(unsigned long));
			if (to_end > 0)
				return -EAGAIN;
			break;

		default:
			retval = -EINVAL;
	}

	return retval;
}

static int motor_fasync(int fd, struct file *file, int on) {
	struct motor_device *motor = file->private_data;

	return fasync_helper(fd, file, on, &motor->fasync);
}

static int motor_open(struct inode *inode, struct file *file)
{
	struct motor_device *motor = find_cdev(inode->i_cdev);
	file->private_data = motor;

	return 0;
}

struct file_operations motor_fops = {
	.owner 		= THIS_MODULE,
	.open = motor_open,
	.unlocked_ioctl = motor_ioctl,
	.fasync		= motor_fasync,
};

static int __init motor_add_one(unsigned int id, unsigned int *params)
{
	int status;
	int ret;

	if ( mot_nump[id] < 3 ) {
		printk(KERN_INFO "stepper: nothing to register for motor %d, too few arguments: %d.\n", id, mot_nump[id]);
		return 0;
	}
/*	Supported args: motX=en,dir,step,limit[,lowpwr,polarity] */
	motor[id].g_enable = params[0];
	motor[id].g_dir = params[1];
	motor[id].g_step = params[2];
	motor[id].g_limit = params[3];
	motor[id].g_lpwr = params[4];
	motor[id].polarity = params[5];

	/* sanity check */
	if ( !( motor[id].g_enable && motor[id].g_dir && motor[id].g_step)) {
		printk(KERN_INFO "stepper: missing parameters, exit driver.\n");
		goto err_para;
	}

	if (gpio_request(motor[id].g_step, "motor-step"))
		return -EINVAL;

	hrtimer_init(&(motor[id].hrt), CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	motor[id].hrt.function = &gpio_timeout;
	motor[id].interval = ktime_set(0, (long) min_ns * 1000);

	if ( gpio_request(motor[id].g_enable, "motor-enable") < 0 ) {
		goto err_gpioenable;
	}
	gpio_direction_output(motor[id].g_enable ,0);
	gpio_set_value (motor[id].g_enable, 1);

	if ( gpio_request(motor[id].g_dir, "motor-ccw") < 0) {
		goto err_gpiodir;
	}
	gpio_direction_output(motor[id].g_dir ,0);


	if (motor[id].g_limit != 0) {
		if ( gpio_request(motor[id].g_limit, "motor-limit") < 0) {
			goto err_gpiolimit;
		} else {
			gpio_direction_input(motor[id].g_limit);
#ifdef CONFIG_MACH_AT91
			at91_set_deglitch(motor[id].g_limit, 1);	/* Enable the glitch filter for interrupt */
#endif
			ret = request_irq(motor[id].g_limit, stepper_irq, 0, DRV_NAME, &(motor[id]));
			if (ret < 0) {
				printk(KERN_INFO "stepper: error requiring .\n");
			}
		}
	} else {
		printk(KERN_INFO "stepper: mot%d is not using limits.\n", id);
	}

	if (motor[id].g_lpwr != 0) {
		if ( gpio_request(motor[id].g_lpwr, "motor-lowpwr") < 0 ) {
			goto err_gpiolwr;
		}
		gpio_direction_output(motor[id].g_lpwr ,0);
	} else {
		printk(KERN_INFO "stepper: mot%d is not using low power function.\n", id);
	}

	/* set to home */
	motor[id].steps = 0;

	mutex_init(&(motor[id].mmutex));

	/* alloc a new device number (major: dynamic, minor: id) */
	status = alloc_chrdev_region(&motor_devno, id, 1, "motor");

	/* create a new char device  */
	motor[id].mcdev = cdev_alloc();
	if(motor[id].mcdev == NULL) {
		status=-ENOMEM;
		goto err_dev;
	}

	motor[id].mcdev->owner = THIS_MODULE;
	motor[id].mcdev->ops = &motor_fops;
	status = cdev_add(motor[id].mcdev, motor_devno, 1);
	if(status){
		goto err_dev;
	}

	device_create(motor_class, NULL, motor_devno, NULL, "motor%d", id);
	printk(KERN_INFO "stepper: motor%d registred on major: %u; minor: %u\n", \
		id, MAJOR(motor_devno), MINOR(motor_devno));

	return 0;

//err:
	printk(KERN_INFO "stepper: err\n");
err_dev:
	printk(KERN_INFO "stepper: err_dev\n");
err_gpiolwr:
	printk(KERN_INFO "stepper: err_gpiolwr\n");
err_gpiolimit:
	printk(KERN_INFO "stepper: err_gpiolimit\n");
err_gpiodir:
	printk(KERN_INFO "stepper: err_gpiodir\n");
err_gpioenable:
	printk(KERN_INFO "stepper: err_gpioenable\n");
//err_gpiostep:
	printk(KERN_INFO "stepper: err_gpiostep ");
//err_pwm:
	printk(KERN_INFO "stepper: err_pwm\n");
err_para:
	printk(KERN_INFO "stepper: Error management not yet implemented. \
		Please reboot your board %d\n",motor[id].g_step);
	return -1;
}

static int __init motor_init(void)
{
	int err;
	struct timespec tp;

	printk(KERN_INFO DRV_DESC ", version " DRV_VERSION "\n");

	hrtimer_get_res(CLOCK_MONOTONIC, &tp);
	min_ns = tp.tv_nsec;
	printk(KERN_INFO "Minimun clock resolution is %ldns, defautl stepper value is min_ns*1000\n", tp.tv_nsec);

	motor = kmalloc(sizeof(struct motor_device) * MAX_MOT_NUM, GFP_KERNEL );

	/*register the class */
	motor_class = class_create(THIS_MODULE, "motor_class");
	if(IS_ERR(motor_class)){
		goto err;
	}

	err = motor_add_one(0, mot0);
	if (err) goto err;

	err = motor_add_one(1, mot1);
	if (err) goto err;

	err = motor_add_one(2, mot2);
	if (err) goto err;

	err = motor_add_one(3, mot3);
	if (err) goto err;

	return 0;

err:
	class_unregister(motor_class);
	return -1;
}

static void __exit motor_exit(void)
{

	class_unregister(motor_class);
}

module_init(motor_init);
module_exit(motor_exit);

MODULE_AUTHOR("Claudio Mignanti <c.mignanti@gmail.com>");
MODULE_DESCRIPTION("Stepper motor driver with pwm emulating over gpio");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:motor-pwm");
