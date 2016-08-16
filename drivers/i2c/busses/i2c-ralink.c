/*
 * drivers/i2c/busses/i2c-ralink.c
 *
 * Copyright (C) 2013 Steven Liu <steven_liu@mediatek.com>
 *
 * Improve driver for i2cdetect from i2c-tools to detect i2c devices on the bus.
 * (C) 2014 Sittisak <sittisaks@hotmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/err.h>

#include <asm/mach-ralink/ralink_regs.h>

#define REG_CONFIG_REG		0x00
#define REG_CLKDIV_REG		0x04
#define REG_DEVADDR_REG		0x08
#define REG_ADDR_REG		0x0C
#define REG_DATAOUT_REG		0x10
#define REG_DATAIN_REG		0x14
#define REG_STATUS_REG		0x18
#define REG_STARTXFR_REG	0x1C
#define REG_BYTECNT_REG		0x20
#define REG_SM0CFG2			0x28
#define REG_SM0CTL0			0x40

#define SYSC_REG_RESET_CTRL	0x34

#define I2C_RST			(1<<16)
#define I2C_STARTERR		BIT(4)
#define I2C_ACKERR		BIT(3)
#define I2C_DATARDY		BIT(2)
#define I2C_SDOEMPTY		BIT(1)
#define I2C_BUSY		BIT(0)

#define I2C_DEVADLEN_7		(6 << 2)
#define I2C_ADDRDIS		BIT(1)

#define CLKDIV_VALUE		200

#define READ_CMD		0x01
#define WRITE_CMD		0x00
#define READ_BLOCK              64

#define SM0CTL0_OD		BIT(31)
#define SM0CTL0_VTRIG		BIT(28)
#define SM0CTL0_OUTHI		BIT(6)
#define SM0CTL0_STRETCH		BIT(1)
#define SM0CTL0_DEFAULT		(SM0CTL0_OD | SM0CTL0_VTRIG | SM0CTL0_OUTHI | SM0CTL0_STRETCH)

#define MAX_SIZE			63

enum {
	I2C_TYPE_RALINK,
	I2C_TYPE_MEDIATEK,
};

static void __iomem *membase;
static struct i2c_adapter *adapter;
static int hw_type;

static void rt_i2c_w32(u32 val, unsigned reg)
{
	iowrite32(val, membase + reg);
}

static u32 rt_i2c_r32(unsigned reg)
{
	return ioread32(membase + reg);
}

static void rt_i2c_default_speed(void)
{
	if (hw_type == I2C_TYPE_RALINK) {
		rt_i2c_w32(CLKDIV_VALUE, REG_CLKDIV_REG);
	} else {
		rt_i2c_w32((CLKDIV_VALUE << 16) | SM0CTL0_DEFAULT, REG_SM0CTL0);
		rt_i2c_w32(1, REG_SM0CFG2);
	}
}

static void rt_i2c_init(struct i2c_adapter *a)
{/*
	u32 val;
	
	val = rt_sysc_r32(SYSC_REG_RESET_CTRL);
	val |= I2C_RST;
	rt_sysc_w32(val, SYSC_REG_RESET_CTRL);
	
	val &= ~I2C_RST;
	rt_sysc_w32(val, SYSC_REG_RESET_CTRL);
*/
	device_reset(a->dev.parent);
	
	udelay(500);
	rt_i2c_w32(I2C_DEVADLEN_7 | I2C_ADDRDIS, REG_CONFIG_REG);
	
	rt_i2c_default_speed();
}

static inline int rt_i2c_wait_rx_done(void)
{
	while (!(rt_i2c_r32(REG_STATUS_REG) & I2C_DATARDY));
	return 0;
}

static inline int rt_i2c_wait_idle(void)
{
	while (rt_i2c_r32(REG_STATUS_REG) & I2C_BUSY);
	return 0;
}

static inline int rt_i2c_wait_tx_done(void)
{
	while (!(rt_i2c_r32(REG_STATUS_REG) & I2C_SDOEMPTY));
	return 0;
}

static int rt_i2c_handle_msg(struct i2c_adapter *a, struct i2c_msg* msg)
{
	int i = 0, j = 0, pos = 0;
	int nblock = msg->len / READ_BLOCK;
	int rem = msg->len % READ_BLOCK;
	int ret = 0;

	if (msg->flags & I2C_M_TEN) {
		printk("10 bits addr not supported\n");
		return -EINVAL;
	}

	if (msg->len > MAX_SIZE) {
		printk("Notice! The FIFO data length is 64 Byte\n");
		return -EINVAL;
	}

	if (msg->flags & I2C_M_RD) {
		for (i = 0; i < nblock; i++) {
			rt_i2c_wait_idle();
			rt_i2c_w32(READ_BLOCK - 1, REG_BYTECNT_REG);
			rt_i2c_w32(READ_CMD, REG_STARTXFR_REG);
			for (j = 0; j < READ_BLOCK; j++) {
				rt_i2c_wait_rx_done();
				msg->buf[pos++] = rt_i2c_r32(REG_DATAIN_REG);
			}
		}

		rt_i2c_wait_idle();
		if (rem) {
			rt_i2c_w32(rem - 1, REG_BYTECNT_REG);
			rt_i2c_w32(READ_CMD, REG_STARTXFR_REG);
		}
		for (i = 0; i < rem; i++) {
			rt_i2c_wait_rx_done();
			msg->buf[pos++] = rt_i2c_r32(REG_DATAIN_REG);
		}
	} else {
		rt_i2c_wait_idle();
		rt_i2c_w32(msg->len - 1, REG_BYTECNT_REG);
		for (i = 0; i < msg->len; i++) {
			rt_i2c_w32(msg->buf[i], REG_DATAOUT_REG);
			if (i == 0)
				rt_i2c_w32(WRITE_CMD, REG_STARTXFR_REG);
			rt_i2c_wait_tx_done();
		}
		//mdelay(2);
	}

	return ret;
}

static int rt_i2c_master_xfer(struct i2c_adapter *a, struct i2c_msg *m, int n)
{
	int i = 0;
	int ret = 0;

	rt_i2c_w32(m->addr, REG_DEVADDR_REG);
	rt_i2c_w32(0, REG_ADDR_REG);

	for (i = 0; ret == 0 && i !=n; i++) {
		ret = rt_i2c_handle_msg(a, &m[i]);

		if (ret < 0) {
			return ret;
		}
	}

	return i;
}

static u32 rt_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rt_i2c_algo = {
	.master_xfer	= rt_i2c_master_xfer,
	.functionality	= rt_i2c_func,
};

static const struct of_device_id i2c_rt_dt_ids[] = {
	{ .compatible = "ralink,rt2880-i2c", .data = (void *) I2C_TYPE_RALINK },
	{ .compatible = "mediatek,mt7628-i2c", .data = (void *) I2C_TYPE_MEDIATEK },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, i2c_rt_dt_ids);

static int rt_i2c_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	const struct of_device_id *match;
	int ret;

	match = of_match_device(i2c_rt_dt_ids, &pdev->dev);
	hw_type = (int) match->data;

	if (!res) {
		dev_err(&pdev->dev, "no memory resource found\n");
		return -ENODEV;
	}

	adapter = devm_kzalloc(&pdev->dev, sizeof(struct i2c_adapter), GFP_KERNEL);
	if (!adapter) {
		dev_err(&pdev->dev, "failed to allocate i2c_adapter\n");
		return -ENOMEM;
	}

	membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(membase))
		return PTR_ERR(membase);

	strlcpy(adapter->name, dev_name(&pdev->dev), sizeof(adapter->name));
	adapter->owner = THIS_MODULE;
	adapter->nr = pdev->id;
	adapter->timeout = HZ;
	adapter->algo = &rt_i2c_algo;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->dev.parent = &pdev->dev;
	adapter->dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(adapter);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, adapter);

	rt_i2c_init(adapter);

	dev_info(&pdev->dev, "loaded\n");

	return 0;
}

static int rt_i2c_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver rt_i2c_driver = {
	.probe		= rt_i2c_probe,
	.remove		= rt_i2c_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "i2c-ralink",
		.of_match_table = i2c_rt_dt_ids,
	},
};

static int __init i2c_rt_init (void)
{
	return platform_driver_register(&rt_i2c_driver);
}
subsys_initcall(i2c_rt_init);

static void __exit i2c_rt_exit (void)
{
	platform_driver_unregister(&rt_i2c_driver);
}

module_exit (i2c_rt_exit);

MODULE_AUTHOR("Steven Liu <steven_liu@mediatek.com>");
MODULE_DESCRIPTION("Ralink I2c host driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:Ralink-I2C");
