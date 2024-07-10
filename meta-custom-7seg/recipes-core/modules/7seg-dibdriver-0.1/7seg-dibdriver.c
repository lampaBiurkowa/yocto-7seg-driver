#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#define DEVICE_NAME "dib7seg"
#define CLASS_NAME "dibbledrv"

static int majorNumber;
static int number = 0;

static struct class*  myDriverClass  = NULL;
static struct device* myDriverDevice = NULL;

static struct gpio_desc *button_inc;
static struct gpio_desc *button_dec;
static struct gpio_desc *seg_pins[8];

static irq_handler_t  irq_handler_inc(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t  irq_handler_dec(unsigned int irq, void *dev_id, struct pt_regs *regs);

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset);
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset);

static struct file_operations fops = {
   .read = dev_read,
   .write = dev_write,
};

static void display_number(int num) {
    static const char digits[10][8] = {
        {1, 1, 1, 1, 1, 1, 0, 0},  // 0
        {0, 1, 1, 0, 0, 0, 0, 0},  // 1
        {1, 1, 0, 1, 1, 0, 1, 1},  // 2
        {1, 1, 1, 1, 0, 0, 1, 0},  // 3
        {0, 1, 1, 0, 0, 1, 1, 0},  // 4
        {1, 0, 1, 1, 0, 1, 1, 0},  // 5
        {1, 0, 1, 1, 1, 1, 1, 1},  // 6
        {1, 1, 1, 0, 0, 0, 0, 0},  // 7
        {1, 1, 1, 1, 1, 1, 1, 0},  // 8
        {1, 1, 1, 1, 0, 1, 1, 0}   // 9
    };

    if (num < 0 || num > 9) return;

    for (int i = 0; i < 8; i++) {
        gpiod_set_value(seg_pins[i], digits[num][i]);
    }
}

static irq_handler_t irq_handler_inc(unsigned int irq, void *dev_id, struct pt_regs *regs) {
    number++;
    if (number > 9) number = 0;
    display_number(number);
    return (irq_handler_t) IRQ_HANDLED;
}

static irq_handler_t irq_handler_dec(unsigned int irq, void *dev_id, struct pt_regs *regs) {
    number--;
    if (number < 0) number = 9;
    display_number(number);
    return (irq_handler_t) IRQ_HANDLED;
}

static int __init my_driver_init(void) {
    int result;
    struct device *dev;
    struct device_node *np;

    pr_info("Initializing the %s device...\n", DEVICE_NAME);

    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        pr_alert("Failed to register device\n");
        return majorNumber;
    }

    myDriverClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(myDriverClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        pr_alert("Failed to register device class\n");
        return PTR_ERR(myDriverClass);
    }

    myDriverDevice = device_create(myDriverClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(myDriverDevice)) {
        class_destroy(myDriverClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        pr_alert("Failed to create the device\n");
        return PTR_ERR(myDriverDevice);
    }

    dev = myDriverDevice;

    np = of_find_node_by_name(NULL, "buttonInc");
    if (!np) {
        pr_alert("Failed to find buttonInc node\n");
        return -ENODEV;
    }
    button_inc = devm_gpiod_get_from_of_node(dev, np, "gpios", 0, GPIOD_IN, "myincbutton");
    
    np = of_find_node_by_name(NULL, "buttonDec");
    if (!np) {
        pr_alert("Failed to find buttonDec node\n");
        return -ENODEV;
    }
    button_dec = devm_gpiod_get_from_of_node(dev, np, "gpios", 0, GPIOD_IN, "mydecbutton");


    if (IS_ERR(button_inc) || IS_ERR(button_dec)) {
        pr_alert("Failed to get buttons\n");
        return PTR_ERR(button_inc);
    }

    for (int i = 0; i < 8; i++) {
        char label[5];
        snprintf(label, sizeof(label), "seg%d", i);
        np = of_find_node_by_name(NULL, label);
        if (!np) {
            pr_alert("Failed to find seg%d node\n", i);
            return -ENODEV;
        }
        seg_pins[i] = devm_gpiod_get_from_of_node(dev, np, "gpios", 0, GPIOD_OUT_LOW, label);
        if (IS_ERR(seg_pins[i])) {
            pr_alert("Failed to get segment %d\n", i);
            return PTR_ERR(seg_pins[i]);
        }
    }

    int irqNumber_inc = gpiod_to_irq(button_inc);
    int irqNumber_dec = gpiod_to_irq(button_dec);
    if (irqNumber_inc < 0 || irqNumber_dec < 0) {
        pr_alert("Failed to get IRQ number buttons\n");
    }

    result = devm_request_irq(dev, irqNumber_inc, (irq_handler_t) irq_handler_inc, IRQF_TRIGGER_FALLING, "button_inc_handler", NULL);
    if (result) {
        pr_alert("Failed to request IRQ for button_inc\n");
        return result;
    }

    result = devm_request_irq(dev, irqNumber_dec, (irq_handler_t) irq_handler_dec, IRQF_TRIGGER_FALLING, "button_dec_handler", NULL);
    if (result) {
        pr_alert("Failed to request IRQ for button_dec\n");
        return result;
    }

    display_number(number);

    pr_info("%s: device class created correctly\n", DEVICE_NAME);
    return 0;
}

static void __exit my_driver_exit(void) {
    for (int i = 0; i < 8; i++) {
        gpiod_put(seg_pins[i]);
    }
    gpiod_put(button_inc);
    gpiod_put(button_dec);
    free_irq(gpiod_to_irq(button_inc), NULL);
    free_irq(gpiod_to_irq(button_dec), NULL);
    device_destroy(myDriverClass, MKDEV(majorNumber, 0));
    class_unregister(myDriverClass);
    class_destroy(myDriverClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    pr_info("%s: device class removed\n", DEVICE_NAME);
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    char buf[8];
    if (*offset > 0) {
        return 0;
    }

    int output = sprintf(buf, "%d\n", number);
    if (copy_to_user(buffer, buf, sizeof(buf))) {
        pr_info("Failed to read number\n"); 
        return 0;
    }

    *offset += sizeof(buf);
    return sizeof(buf);
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    char buf[8];
    if (len >= 8) len = 7; //one sign for \0
    if (copy_from_user(buf, buffer, len)) {
        return -EFAULT;
    }
    buf[len] = '\0';
    kstrtoint(buf, 10, &number);
    if (number < 0 || number > 9) number = 0;
    display_number(number);

    return len;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lampaBiurkowa");
MODULE_DESCRIPTION("A simple 7seg driver for BeagleBone Green");

module_init(my_driver_init);
module_exit(my_driver_exit);