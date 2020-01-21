// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2020
 * Yasir Khan, himself, yasir_electronics@yahoo.com
 *
 */

#include <common.h>
#include <bootretry.h>
#include <cli.h>
#include <command.h>
#include <console.h>
#include <dm.h>
#include <environment.h>
#include <errno.h>
#include <malloc.h>
#include <asm/byteorder.h>
#include <linux/compiler.h>
#include <serial.h>
#include <watchdog.h>

#define DEFAULT_SERIAL_DEVICE 0x1

static struct udevice *serial_cur_dev;

static int serial_configure(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int ret;

	if (ops) {
		ret = ops->setconfig(dev, SERIAL_DEFAULT_CONFIG);
		if (ret) {
			printf("Failed to initialize serial contoller. ret: %d\n", ret);
			return -1;
		}

		ret = ops->setbrg(dev, 115200);
		if (ret) {
			printf("Failed to set serial baudrate. ret: %d\n", ret);
			return -1;
		}
	}

	return 0;
}

static int serial_read_char(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int err;

	do {
		err = ops->getc(dev);
		if (err == -EAGAIN)
			WATCHDOG_RESET();
	} while (err == -EAGAIN);

	return err >= 0 ? err : 0;
}

static void serial_write_char(struct udevice *dev, char ch)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int err;
	
	if (ch == '\n')
		serial_write_char(dev, '\r');

	do {
		err = ops->putc(dev, ch);
	} while (err == -EAGAIN);
}


static int serial_set_dev_num(unsigned int devnum)
{
	struct udevice *serial_dev;
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_SERIAL, devnum, &serial_dev);
	if (ret) {
		debug("%s: No serial device %d\n", __func__, devnum);
		return ret;
	}

        serial_cur_dev = serial_dev;

	serial_configure(serial_dev);

        return 0;
}

static int serial_get_cur_device(struct udevice **serial_dev)
{
	if (!serial_cur_dev) {
		if (serial_set_dev_num(DEFAULT_SERIAL_DEVICE)) {
			printf("Default serial device  %d not found\n",
				DEFAULT_SERIAL_DEVICE);
                        return -ENODEV;
		}
	}

	if (!serial_cur_dev) {
		printf("No serial device selected\n");
		return -ENODEV;
	}

	*serial_dev = serial_cur_dev;

        return 0;
}

static int do_serial_read(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	uint	length;
	u_char  *memaddr;
	int ret;
	struct udevice *dev;

	if (argc != 3)
		return CMD_RET_USAGE;

	/*
	 * Length is the number of bytes.
	 */
	length = simple_strtoul(argv[2], NULL, 16);

	/*
	 * memaddr is the address where to store things in memory
	 */
	memaddr = (u_char *)simple_strtoul(argv[1], NULL, 16);

	printf("Trying to read %d bytes\n", length);

	ret = serial_get_cur_device(&dev);
	if (ret) {
		printf("Serial device not found\n");
		return CMD_RET_FAILURE;
	}

	while (length-- > 0) {
		char c;
		
		c = serial_read_char(dev);
		
		*memaddr = c;

		memaddr++;
		//udelay(11000);
	}

	return 0;
}

static int do_serial_write(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	uint length;
	u_char  *memaddr;
	int ret;
	struct udevice *dev;

	if (argc < 3)
		return cmd_usage(cmdtp);

	/*
	 * memaddr is the address where to store things in memory
	 */
	memaddr = (u_char *)simple_strtoul(argv[1], NULL, 16);

	/*
	 * Length is the number of bytes.
	 */
	length = simple_strtoul(argv[2], NULL, 16);

	printf("Trying to write %d bytes\n", length);

	ret = serial_get_cur_device(&dev);
	if (ret) {
		printf("Serial device not found\n");
		return CMD_RET_FAILURE;
	}

	while (length-- > 0) {
		char c = *memaddr;
		
		serial_write_char(dev, c);
		memaddr++;
		//udelay(11000);
	}

	return 0;
}

/**
 * do_serial_dev_num() - Handle the "serial dev" command-line command
 * @cmdtp:	Command data struct pointer
 * @flag:	Command flag
 * @argc:	Command-line argument count
 * @argv:	Array of command-line arguments
 *
 * Returns zero on success, CMD_RET_USAGE in case of misuse and negative
 * on error.
 */
static int do_serial_dev_num(cmd_tbl_t *cmdtp, int flag, int argc,
				char * const argv[])
{
	int	ret = 0;
	int	devnum;

	if (argc == 1) {
		/* querying current setting */
		struct udevice *serial_dev;

		if (!serial_get_cur_device(&serial_dev))
			devnum = serial_dev->seq;
		else
			devnum = -1;

		printf("Current serial port is %d\n", devnum);
	} else {
		devnum = simple_strtoul(argv[1], NULL, 10);

		printf("Setting serial to %d\n", devnum);

		ret = serial_set_dev_num(devnum);
		if (ret)
			printf("Failure changing serial number (%d)\n", ret);
	}

	return ret ? CMD_RET_FAILURE : 0;
}

static cmd_tbl_t cmd_serial_sub[] = {
	U_BOOT_CMD_MKENT(dev, 2, 0, do_serial_dev_num, "", ""),
	U_BOOT_CMD_MKENT(write, 3, 0, do_serial_write, "", ""),
	U_BOOT_CMD_MKENT(read, 3, 0, do_serial_read, "", "")
};

/**
 * do_serial() - Handle the "serial" command-line command
 * @cmdtp:	Command data struct pointer
 * @flag:	Command flag
 * @argc:	Command-line argument count
 * @argv:	Array of command-line arguments
 *
 * Returns zero on success, CMD_RET_USAGE in case of misuse and negative
 * on error.
 */
static int do_serial(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
{
	cmd_tbl_t *c;

	if (argc < 2)
		return CMD_RET_USAGE;

	/* Strip off leading 'serial' command argument */
	argc--;
	argv++;

	c = find_cmd_tbl(argv[0], &cmd_serial_sub[0], ARRAY_SIZE(cmd_serial_sub));

	if (c)
		return c->cmd(cmdtp, flag, argc, argv);
	else
		return CMD_RET_USAGE;
}

/***************************************************/
#ifdef CONFIG_SYS_LONGHELP
static char serial_help_text[] =
	"dev [dev] - show or set current uart port\n"
	"serial write memaddress length - write bytes\n"
	"serial read memaddress length  - read bytes\n";
#endif

U_BOOT_CMD(
	serial, 4, 0, do_serial,
	"Serial UART sub-system",
	serial_help_text
);
