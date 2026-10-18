#include<linux/init.h>
#include<linux/module.h>
#include<linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/gpio/machine.h>

#define DRV_NAME "rotary-enc-sim"

struct rotary_enc {
	struct input_dev *input;
	struct gpio_desc *gpio_a;
	struct gpio_desc *gpio_b;
	int irq_a;
	int irq_b;
	struct mutex lock;
	unsigned int last_state;
	int acc;
};

static struct gpiod_lookup_table enc_gpios = {
      .dev_id = DRV_NAME,
      .table = {
          GPIO_LOOKUP("rotary-sim", 0, "a", GPIO_ACTIVE_HIGH),
          GPIO_LOOKUP("rotary-sim", 1, "b", GPIO_ACTIVE_HIGH),
          { }
      },
  };

#define INPUT_NAME "rotary-encoder-gpio"

static unsigned int steps_per_detent = 4;
module_param(steps_per_detent, uint, 0444);
MODULE_PARM_DESC(steps_per_detent, "Quarter steps per detent (1-4), default 4");

static bool invert;
module_param(invert, bool, 0444);
MODULE_PARM_DESC(invert, "Invert rotation direction, default false");

static const s8 trans[4][4] = {
	[0] = {  0, -1,  1,  0 },
	[1] = {  1,  0,  0, -1 },
	[2] = { -1,  0,  0,  1 },
	[3] = {  0,  1, -1,  0 },
};


static int rotary_read_state(struct rotary_enc *enc)
{
	int a = gpiod_get_value_cansleep(enc->gpio_a);
	int b = gpiod_get_value_cansleep(enc->gpio_b);
	if (a<0) return a;
	if (b<0) return b;
	return (a<<1) | b;
}

static irqreturn_t rotary_irq(int irq,void *data){
	struct rotary_enc *enc =data;
	int state;
	int delta;
	mutex_lock(&enc->lock);

	state = rotary_read_state(enc);
	if(state<0)
	{
		mutex_unlock(&enc->lock);
		return IRQ_HANDLED;
	}
	delta = trans[enc->last_state][state];
	enc->last_state = state;
	if(delta != 0)
	{
		enc->acc += delta;
		if(enc->acc >= (int)steps_per_detent)
		{
			input_report_rel(enc->input,REL_X,invert ? -1 : 1);
			input_sync(enc->input);
			enc->acc = 0;
		} else if (enc->acc <= -(int)steps_per_detent){
			input_report_rel(enc->input,REL_X,invert ?  1 : -1);
			input_sync(enc->input);
			enc->acc = 0;
		}
	}
	mutex_unlock(&enc->lock);
	return IRQ_HANDLED;
	}
static int rotary_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	struct device *dev = &pdev->dev;
	struct rotary_enc *enc;
	int ret,state;
	if(steps_per_detent<1 || steps_per_detent > 4){
		dev_err(dev,"err!must 1-4");
		return -EINVAL;
	}
	enc = devm_kzalloc(dev, sizeof(*enc),GFP_KERNEL);
	if(!enc)
		return -ENOMEM;
	mutex_init(&enc->lock);
	enc->gpio_a = devm_gpiod_get(dev,"a",GPIOD_IN);
	if(IS_ERR(enc->gpio_a))
	{
		return dev_err_probe(dev,PTR_ERR(enc->gpio_a),"err gpio_a");
	}
	enc->gpio_b = devm_gpiod_get(dev, "b", GPIOD_IN);
	if(IS_ERR(enc->gpio_b))
	{
		return dev_err_probe(dev,PTR_ERR(enc->gpio_b),"err gpio_b");
	}

	state = rotary_read_state(enc);
	if(state < 0)
		return state;
	enc->last_state = state;
	enc->acc = 0;

	enc->irq_a = gpiod_to_irq(enc->gpio_a);
	if(enc->irq_a<0)
		return dev_err_probe(dev,enc->irq_a,"err irq_a");
	enc->irq_b = gpiod_to_irq(enc->gpio_b);
	if(enc->irq_b<0)
		return dev_err_probe(dev,enc->irq_b,"err irq_b");

	input = devm_input_allocate_device(dev);
	if(!input)
		return -ENOMEM;
	input->name = INPUT_NAME;
	input->phys = "rotary-encoder-gpio/input0";
	input->id.bustype = BUS_HOST;
	input_set_capability(input,EV_REL,REL_X);
	ret = input_register_device(input);
	if(ret)
		return dev_err_probe(dev,ret,"err register input");
	enc->input= input;

	ret = devm_request_threaded_irq(dev,enc->irq_a,NULL,rotary_irq,IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,DRV_NAME,enc);
	if(ret)
		return dev_err_probe(dev,ret,"err request irq_a");
	ret = devm_request_threaded_irq(dev,enc->irq_b,NULL,rotary_irq,IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,DRV_NAME,enc);
	if(ret)
		return dev_err_probe(dev,ret,"err request irq_b");
	return 0;

}

static struct platform_driver rotary_driver = {
        .probe = rotary_probe,
        .driver = { .name = DRV_NAME},
};

static struct platform_device *rotary_pdev;

static int __init rotary_init(void)
{
	gpiod_add_lookup_table(&enc_gpios);
	rotary_pdev = platform_device_register_simple(DRV_NAME,PLATFORM_DEVID_NONE,NULL,0);
	pr_info("rotary init");
	return platform_driver_register(&rotary_driver);
}

static void __exit rotary_exit(void)
{
	platform_driver_unregister(&rotary_driver);
        platform_device_unregister(rotary_pdev);
	gpiod_remove_lookup_table(&enc_gpios);
	pr_info("rotary exit");
}

module_init(rotary_init);
module_exit(rotary_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO rotary encoder input driver");
