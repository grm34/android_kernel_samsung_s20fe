/*
 * LED driver - leds-ktd2692.c
 *
 * Copyright (C) 2013 Sunggeun Yim <sunggeun.yim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pwm.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/leds-ktd2692.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_LEDS_KTD2692_MULTI_TORCH_REAR3
extern struct device *camera_flash_dev;
#else
extern struct class *camera_class; /*sys/class/camera*/
struct device *camera_flash_dev;
#endif
struct ktd2692_platform_data *global_ktd2692data;
struct device *global_dev;

void ktd2692_setGpio(int onoff)
{
	if (onoff) {
		__gpio_set_value(global_ktd2692data->flash_control, 1);
	} else {
		__gpio_set_value(global_ktd2692data->flash_control, 0);
	}
}

void ktd2692_set_low_bit(void)
{
	__gpio_set_value(global_ktd2692data->flash_control, 0);
	udelay(T_L_LB);	/* 80us */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_H_LB);	/* 5us */
}

void ktd2692_set_high_bit(void)
{
	__gpio_set_value(global_ktd2692data->flash_control, 0);
	udelay(T_L_HB);	/* 5us */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_H_HB);	/* 80us */
}

static int ktd2692_set_bit(unsigned int bit)
{
	if (bit) {
		ktd2692_set_high_bit();
	} else {
		ktd2692_set_low_bit();
	}
	return 0;
}

static int ktd2692_write_data(unsigned data)
{
	int err = 0;
	unsigned int bit = 0;

	/* Data Start Condition */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	ndelay(T_SOD*1000); //15us

	/* BIT 7*/
	bit = ((data>> 7) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 6 */
	bit = ((data>> 6) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 5*/
	bit = ((data>> 5) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 4 */
	bit = ((data>> 4) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 3*/
	bit = ((data>> 3) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 2 */
	bit = ((data>> 2) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 1*/
	bit = ((data>> 1) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 0 */
	bit = ((data>> 0) & 0x01);
	ktd2692_set_bit(bit);

	 __gpio_set_value(global_ktd2692data->flash_control, 0);
	ndelay(T_EOD_L*1000); //4us

	/* Data End Condition */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_EOD_H);

	return err;
}

ssize_t ktd2692_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	int value = 0;
	int ret = 0;
	unsigned long flags = 0;
	struct pinctrl *pinctrl;
	int index = 0;

	if ((buf == NULL) || kstrtouint(buf, 10, &value)) {
		return -1;
	}

	global_ktd2692data->sysfs_input_data = value;

	if (value <= 0) {
		ret = gpio_request(global_ktd2692data->flash_control, "ktd2692_led_control");
		if (ret) {
			LED_ERROR("Failed to requeset ktd2692_led_control\n");
		} else {
			LED_INFO("KTD2692-TORCH OFF. : E(%d)\n", value);

			global_ktd2692data->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;
			spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
			ktd2692_write_data(global_ktd2692data->mode_status|
								KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
			spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

			ktd2692_setGpio(0);
			gpio_free(global_ktd2692data->flash_control);
			LED_INFO("KTD2692-TORCH OFF. : X(%d)\n", value);
		}

		pinctrl = devm_pinctrl_get_select(global_dev, "idle");
		if (IS_ERR(pinctrl))
			pr_err("%s: flash %s pins are not configured\n", __func__, "idle");

	} else {
		pinctrl = devm_pinctrl_get_select(global_dev, "host");
		if (IS_ERR(pinctrl))
			pr_err("%s: flash %s pins are not configured\n", __func__, "host");

		ret = gpio_request(global_ktd2692data->flash_control, "ktd2692_led_control");
		if (ret) {
			LED_ERROR("Failed to requeset ktd2692_led_control\n");
		} else {
			if (value == 1) {
				global_ktd2692data->movie_current_value = global_ktd2692data->torch_current_value;
			} else if (value == 100) {
				global_ktd2692data->movie_current_value = global_ktd2692data->factory_torch_current_value;
			} else if (1001 <= value && value <= 1010) {
				index = value - 1001;
				global_ktd2692data->movie_current_value = global_ktd2692data->torch_table[index];
			} else {
				pr_info("[LED KTD2692]%s , Invalid value:%d\n", __func__, value);
			}

			LED_INFO("KTD2692-TORCH ON. : E(%d) (0x%x)\n", value, global_ktd2692data->movie_current_value);

			global_ktd2692data->mode_status = KTD2692_ENABLE_MOVIE_MODE;
			spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
			ktd2692_write_data(global_ktd2692data->LVP_Voltage|
								KTD2692_ADDR_LVP_SETTING);
#if 0	/* use the internel defualt setting */
			ktd2692_write_data(global_ktd2692data->flash_timeout|
								KTD2692_ADDR_FLASH_TIMEOUT_SETTING);
#endif
			ktd2692_write_data(global_ktd2692data->movie_current_value|
								KTD2692_ADDR_MOVIE_CURRENT_SETTING);
			ktd2692_write_data(global_ktd2692data->mode_status|
								KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
			spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

			gpio_free(global_ktd2692data->flash_control);
			LED_INFO("KTD2692-TORCH ON. : X(%d)\n", value);
		}
	}

	if (!IS_ERR(pinctrl))
		devm_pinctrl_put(pinctrl);

	return count;
}

ssize_t ktd2692_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", global_ktd2692data->sysfs_input_data);
}

#ifdef CONFIG_LEDS_KTD2692_MULTI_TORCH_REAR3
static DEVICE_ATTR(rear_flash3, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
	ktd2692_show, ktd2692_store);
#else
static DEVICE_ATTR(rear_flash, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
	ktd2692_show, ktd2692_store);
#endif

static int ktd2692_parse_dt(struct device *dev,
                                struct ktd2692_platform_data *pdata)
{
	struct device_node *dnode = dev->of_node;
	int ret = 0;

	/* Defulat Value */
	pdata->LVP_Voltage = KTD2692_DISABLE_LVP;
	pdata->flash_timeout = KTD2692_TIMER_1049ms;	/* default */
	pdata->min_current_value = KTD2692_MIN_CURRENT_240mA;
	pdata->movie_current_value = KTD2692_MOVIE_CURRENT6;
	pdata->flash_current_value = KTD2692_FLASH_CURRENT16;
	pdata->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;

	/* get gpio */
	pdata->flash_control = of_get_named_gpio(dnode, "flash_control", 0);
	if (!gpio_is_valid(pdata->flash_control)) {
		dev_err(dev, "failed to get flash_control\n");
		return -1;
	}

	cfg->max_brightness = offset;
}

static void ktd2692_init_flash_timeout(struct led_classdev_flash *fled_cdev,
				       struct ktd2692_led_config_data *cfg)
{
	struct led_flash_setting *setting;

	setting = &fled_cdev->timeout;
	setting->min = KTD2692_FLASH_MODE_TIMEOUT_DISABLE;
	setting->max = cfg->flash_max_timeout;
	setting->step = cfg->flash_max_timeout
			/ (KTD2692_FLASH_MODE_TIMEOUT_LEVELS - 1);
	setting->val = cfg->flash_max_timeout;
}

static void ktd2692_setup(struct ktd2692_context *led)
{
	led->mode = KTD2692_MODE_DISABLE;
	ktd2692_expresswire_reset(led);
	gpiod_direction_output(led->aux_gpio, KTD2692_LOW);

	ktd2692_expresswire_write(led, (KTD2962_MM_MIN_CURR_THRESHOLD_SCALE - 1)
				 | KTD2692_REG_MM_MIN_CURR_THRESHOLD_BASE);
	ktd2692_expresswire_write(led, KTD2692_FLASH_MODE_CURR_PERCENT(45)
				 | KTD2692_REG_FLASH_CURRENT_BASE);
}

static void regulator_disable_action(void *_data)
{
	struct device *dev = _data;
	struct ktd2692_context *led = dev_get_drvdata(dev);
	int ret;

	ret = regulator_disable(led->regulator);
	if (ret)
		dev_err(dev, "Failed to disable supply: %d\n", ret);
}

static int ktd2692_parse_dt(struct ktd2692_context *led, struct device *dev,
			    struct ktd2692_led_config_data *cfg)
{
	struct device_node *np = dev->of_node;
	struct device_node *child_node;
	int ret;

	if (!dev->of_node)
		return -ENXIO;

	led->ctrl_gpio = devm_gpiod_get(dev, "ctrl", GPIOD_ASIS);
	ret = PTR_ERR_OR_ZERO(led->ctrl_gpio);
	if (ret) {
		pr_info("%s failed to get a torch_current_value\n", __func__);
	}  

	ret = of_property_read_u32(dnode, "factory_torch_current_value", &(pdata->factory_torch_current_value));
	if (ret) {
		pr_info("%s failed to get a factory_torch_current_value\n", __func__);
	}

	led->regulator = devm_regulator_get(dev, "vin");
	if (IS_ERR(led->regulator))
		led->regulator = NULL;

	if (led->regulator) {
		ret = regulator_enable(led->regulator);
		if (ret) {
			dev_err(dev, "Failed to enable supply: %d\n", ret);
		} else {
			ret = devm_add_action_or_reset(dev,
						regulator_disable_action, dev);
			if (ret)
				return ret;
		}
	}

	child_node = of_get_next_available_child(np, NULL);
	if (!child_node) {
		dev_err(dev, "No DT child node found for connected LED.\n");
		return -EINVAL;
	}

	led->fled_cdev.led_cdev.name =
		of_get_property(child_node, "label", NULL) ? : child_node->name;

	ret = of_property_read_u32(child_node, "led-max-microamp",
				   &cfg->movie_max_microamp);
	if (ret) {
		pr_info("%s failed to get torch_table\n", __func__);
	}

	return ret;
}

static int ktd2692_probe(struct platform_device *pdev)
{
	struct ktd2692_platform_data *pdata;
	int ret = 0;
	static bool probe_retried = false;

	if (!camera_flash_dev) {
		goto probe_defer;
	}

	if (pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		ret = ktd2692_parse_dt(&pdev->dev, pdata);
		if (ret < 0) {
			return -EFAULT;
		}
	} else {
	pdata = pdev->dev.platform_data;
		if (pdata == NULL) {
			return -EFAULT;
		}
	}

	global_ktd2692data = pdata;
	global_dev = &pdev->dev;

	LED_INFO("KTD2692_LED Probe\n");

#ifdef CONFIG_LEDS_KTD2692_MULTI_TORCH_REAR3
	if (device_create_file(camera_flash_dev, &dev_attr_rear_flash3) < 0) {
		LED_ERROR("failed to create device file, %s\n",
				dev_attr_rear_flash3.attr.name);
	}
#else 
	camera_flash_dev = device_create(camera_class, NULL, 0, NULL, "flash");
	if (IS_ERR(camera_flash_dev)) {
		LED_ERROR("Failed to create device(flash)!\n");
	}

	if (device_create_file(camera_flash_dev, &dev_attr_rear_flash) < 0) {
		LED_ERROR("failed to create device file, %s\n",
				dev_attr_rear_torch_flash.attr.name);
	}
#endif
	spin_lock_init(&pdata->int_lock);

	return 0;

probe_defer:
	if (probe_retried) {
		LED_ERROR("probe has already been retried!!");
		BUG();
	}

	probe_retried = true;
	LED_ERROR("camera_flash_dev is not yet probed");
	return -EPROBE_DEFER;

}

static void ktd2692_shutdown(struct device *dev)
{
	int ret = 0;
	unsigned long flags = 0;
	struct pinctrl *pinctrl;

	ret = gpio_request(global_ktd2692data->flash_control, "ktd2692_led_control");
	if (ret) {
		LED_ERROR("Failed to requeset ktd2692_led_control\n");
	} else {
		LED_INFO("KTD2692-TORCH OFF. : E\n");

		global_ktd2692data->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;
		spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
		ktd2692_write_data(global_ktd2692data->mode_status|
						KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
		spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

		ktd2692_setGpio(0);
		gpio_free(global_ktd2692data->flash_control);
		LED_INFO("KTD2692-TORCH OFF. : X\n");
	}

	pinctrl = devm_pinctrl_get_select(global_dev, "idle");
	if (IS_ERR(pinctrl))
		pr_err("%s: flash %s pins are not configured\n", __func__, "idle");
}

//static int __devexit ktd2692_remove(struct platform_device *pdev)
static int ktd2692_remove(struct platform_device *pdev)
{
	struct ktd2692_context *led = platform_get_drvdata(pdev);

	led_classdev_flash_unregister(&led->fled_cdev);

	mutex_destroy(&led->lock);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ktd2692_dt_ids[] = {
	{ .compatible = "ktd2692",},
	{},
};
/*MODULE_DEVICE_TABLE(of, ktd2692_dt_ids);*/
#endif

static struct platform_driver ktd2692_driver = {
	.driver = {
	.name = ktd2692_NAME,
	.owner = THIS_MODULE,
#ifdef CONFIG_OF
	.of_match_table = ktd2692_dt_ids,
#endif
	.shutdown = ktd2692_shutdown,
	},
	.probe = ktd2692_probe,
	.remove = ktd2692_remove,
};

static int __init ktd2692_init(void)
{
	return platform_driver_register(&ktd2692_driver);
}

static void __exit ktd2692_exit(void)
{
	platform_driver_unregister(&ktd2692_driver);
}

module_init(ktd2692_init);
module_exit(ktd2692_exit);

MODULE_AUTHOR("sunggeun yim <sunggeun.yim@samsung.com.com>");
MODULE_DESCRIPTION("KTD2692 driver");
MODULE_LICENSE("GPL");

