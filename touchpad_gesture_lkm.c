#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>

#define DRIVER_NAME "touchpad_gesture"
#define GESTURE_ZONE_RIGHT 0
#define GESTURE_ZONE_LEFT 1

struct gesture_device {
    struct device *dev; 
    struct input_dev *input_dev;
    struct input_handle *touchpad_handle; 
    int finger_count;
    int start_x, start_y;
    int current_x, current_y;
    int zone;
    bool active;
    bool gesture_triggered;
    
    int tp_min_x, tp_max_x;
    int tp_min_y, tp_max_y;
    int zone_threshold;
};

static struct gesture_device *gesture_dev;

static void init_touchpad_params(struct input_dev *dev) {
    gesture_dev->tp_min_x = dev->absinfo[ABS_MT_POSITION_X].minimum;
    gesture_dev->tp_max_x = dev->absinfo[ABS_MT_POSITION_X].maximum;
    gesture_dev->tp_min_y = dev->absinfo[ABS_MT_POSITION_Y].minimum;
    gesture_dev->tp_max_y = dev->absinfo[ABS_MT_POSITION_Y].maximum;
    
    int tp_width = gesture_dev->tp_max_x - gesture_dev->tp_min_x;
    gesture_dev->zone_threshold = tp_width / 3;
    
    printk(KERN_INFO "Touchpad parameters:\n");
    printk(KERN_INFO "  X range: %d - %d (width: %d)\n", 
           gesture_dev->tp_min_x, gesture_dev->tp_max_x, tp_width);
    printk(KERN_INFO "  Y range: %d - %d\n", 
           gesture_dev->tp_min_y, gesture_dev->tp_max_y);
    printk(KERN_INFO "  Zone threshold: %d\n", gesture_dev->zone_threshold);
}

static int determine_zone(int x_pos) {
    if (x_pos > gesture_dev->tp_max_x - gesture_dev->zone_threshold) {
        return GESTURE_ZONE_RIGHT;
    } else if (x_pos < gesture_dev->tp_min_x + gesture_dev->zone_threshold) {
        return GESTURE_ZONE_LEFT;
    }
    return -1;
}

static void process_touch_event(struct input_handle *handle, 
                               unsigned int type, 
                               unsigned int code, 
                               int value) {
    
    if (type == EV_ABS) {
        switch (code) {
            case ABS_MT_SLOT:
                break;
            case ABS_MT_TRACKING_ID:
                if (value == -1) {
                    gesture_dev->finger_count--;
                    if (gesture_dev->finger_count < 0) 
                        gesture_dev->finger_count = 0;
                } else {
                    if (gesture_dev->finger_count == 0) {
                        gesture_dev->start_x = gesture_dev->current_x;
                        gesture_dev->start_y = gesture_dev->current_y;
                        gesture_dev->active = false;
                        gesture_dev->gesture_triggered = false;
                    }
                    gesture_dev->finger_count++;
                }
                break;
            case ABS_MT_POSITION_X:
                gesture_dev->current_x = value;
                if (gesture_dev->finger_count == 1) {
                    gesture_dev->start_x = value;
                    gesture_dev->zone = determine_zone(value);
                }
                break;
            case ABS_MT_POSITION_Y:
                gesture_dev->current_y = value;
                if (gesture_dev->finger_count == 1) {
                    gesture_dev->start_y = value;
                }
                break;
        }
    }
    
    if (gesture_dev->finger_count == 2 && !gesture_dev->active) {
        int delta_x = abs(gesture_dev->current_x - gesture_dev->start_x);
        int delta_y = abs(gesture_dev->current_y - gesture_dev->start_y);
        int activation_threshold = (gesture_dev->tp_max_y - gesture_dev->tp_min_y) / 20;
        
        if (delta_y > activation_threshold && delta_y > delta_x) {
            gesture_dev->active = true;
        }
    }
    
    if (gesture_dev->active && gesture_dev->finger_count == 2 && 
        gesture_dev->zone != -1 && !gesture_dev->gesture_triggered) {
        
        int delta_x = abs(gesture_dev->current_x - gesture_dev->start_x);
        int delta_y = gesture_dev->current_y - gesture_dev->start_y;
        int gesture_threshold = (gesture_dev->tp_max_y - gesture_dev->tp_min_y) / 12;

        if (abs(delta_y) > gesture_threshold && abs(delta_y) > delta_x) {
            int gesture_code;
            
            if (gesture_dev->zone == GESTURE_ZONE_RIGHT) {
                gesture_code = (delta_y < 0) ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;
            } else {
                gesture_code = (delta_y < 0) ? KEY_BRIGHTNESSUP : KEY_BRIGHTNESSDOWN;
            }
            
            input_report_key(gesture_dev->input_dev, gesture_code, 1);
            input_report_key(gesture_dev->input_dev, gesture_code, 0);
            input_sync(gesture_dev->input_dev);
            
            printk(KERN_DEBUG "Touchpad gesture: %s in %s zone (delta_y: %d)\n",
                   (delta_y < 0) ? "UP" : "DOWN",
                   (gesture_dev->zone == GESTURE_ZONE_RIGHT) ? "RIGHT" : "LEFT",
                   delta_y);
            
            gesture_dev->gesture_triggered = true;
            gesture_dev->start_y = gesture_dev->current_y;
        }
    }
    
    if (gesture_dev->finger_count == 0) {
        gesture_dev->active = false;
        gesture_dev->gesture_triggered = false;
    }
}

static int input_connect(struct input_handler *handler,
                        struct input_dev *dev,
                        const struct input_device_id *id) {
    struct input_handle *handle;
    int error;
    
    printk(KERN_INFO "Gesture driver probing device: %s\n", dev->name);
    
    if (!strstr(dev->name, "MSFT0001") && !strstr(dev->name, "04F3:31BE")) {
        printk(KERN_DEBUG "Gesture driver: Not our touchpad, skipping\n");
        return -ENODEV;
    }
    
    if (!(dev->evbit[0] & BIT(EV_ABS)) ||
        !(dev->absbit[0] & BIT(ABS_MT_POSITION_X)) ||
        !(dev->absbit[0] & BIT(ABS_MT_POSITION_Y)) ||
        !dev->absinfo[ABS_MT_POSITION_X].maximum) {
        printk(KERN_DEBUG "Gesture driver: Missing required capabilities\n");
        return -ENODEV;
    }
    
    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "touchpad_gesture";
    
    error = input_register_handle(handle);
    if (error)
        goto err_free_handle;
    
    error = input_open_device(handle);
    if (error)
        goto err_unregister_handle;
    
    gesture_dev->touchpad_handle = handle;
    init_touchpad_params(dev);
    
    printk(KERN_INFO "Touchpad gesture driver connected to %s\n", dev->name);
    return 0;
    
err_unregister_handle:
    input_unregister_handle(handle);
err_free_handle:
    kfree(handle);
    return error;
}

static void input_disconnect(struct input_handle *handle) {
    printk(KERN_INFO "Touchpad gesture driver disconnected from %s\n", 
           handle->dev->name);
    
    if (gesture_dev->touchpad_handle == handle) {
        gesture_dev->touchpad_handle = NULL;
    }
    
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id gesture_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT(EV_ABS) },
    },
    { },
};

MODULE_DEVICE_TABLE(input, gesture_ids);

static struct input_handler gesture_handler = {
    .event          = process_touch_event,
    .connect        = input_connect,
    .disconnect     = input_disconnect,
    .name           = DRIVER_NAME,
    .id_table       = gesture_ids,
};

static int gesture_probe(struct platform_device *pdev)
{
    int ret;
    
    printk(KERN_INFO "=== GESTURE PROBE CALLED ===\n");
    printk(KERN_INFO "Device: %s\n", dev_name(&pdev->dev));
    
    gesture_dev = devm_kzalloc(&pdev->dev, sizeof(struct gesture_device), GFP_KERNEL);
    if (!gesture_dev)
        return -ENOMEM;
    
    gesture_dev->dev = &pdev->dev;
    
    gesture_dev->input_dev = devm_input_allocate_device(&pdev->dev);
    if (!gesture_dev->input_dev) {
        printk(KERN_ERR "Failed to allocate input device\n");
        return -ENOMEM;
    }
    
    gesture_dev->input_dev->name = "Touchpad Gesture Controller";
    
    set_bit(EV_KEY, gesture_dev->input_dev->evbit);
    set_bit(KEY_VOLUMEUP, gesture_dev->input_dev->keybit);
    set_bit(KEY_VOLUMEDOWN, gesture_dev->input_dev->keybit);
    set_bit(KEY_BRIGHTNESSUP, gesture_dev->input_dev->keybit);
    set_bit(KEY_BRIGHTNESSDOWN, gesture_dev->input_dev->keybit);
    
    ret = input_register_device(gesture_dev->input_dev);
    if (ret) {
        printk(KERN_ERR "Failed to register input device\n");
        return ret;
    }
    
    ret = input_register_handler(&gesture_handler);
    if (ret) {
        printk(KERN_ERR "Failed to register input handler\n");
        input_unregister_device(gesture_dev->input_dev);
        return ret;
    }
    
    dev_set_drvdata(&pdev->dev, gesture_dev);
    
    printk(KERN_INFO "=== GESTURE PROBE SUCCESS ===\n");
    return 0;
}

static void gesture_remove(struct platform_device *pdev)
{
    printk(KERN_INFO "=== GESTURE REMOVE CALLED ===\n");
    
    input_unregister_handler(&gesture_handler);
    
    if (gesture_dev && gesture_dev->input_dev) {
        input_unregister_device(gesture_dev->input_dev);
    }
    
    gesture_dev = NULL;
}

static int gesture_suspend(struct platform_device *pdev, pm_message_t state)
{
    printk(KERN_INFO "=== GESTURE SUSPEND CALLED ===\n");
    return 0;
}

static int gesture_resume(struct platform_device *pdev)
{
    printk(KERN_INFO "=== GESTURE RESUME CALLED ===\n");
    return 0;
}

static struct platform_driver gesture_platform_driver = {
    .probe = gesture_probe,
    .remove = gesture_remove,
    .suspend = gesture_suspend,
    .resume = gesture_resume,
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
};

static struct platform_device *gesture_test_device;

static int __init gesture_driver_init(void)
{
    int ret;
    
    printk(KERN_INFO "=== GESTURE DRIVER INIT ===\n");
    
    ret = platform_driver_register(&gesture_platform_driver);
    if (ret) {
        printk(KERN_ERR "Failed to register platform driver: %d\n", ret);
        return ret;
    }
    
    gesture_test_device = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
    if (IS_ERR(gesture_test_device)) {
        printk(KERN_WARNING "Failed to create test device, probe won't be called automatically\n");
    }
    
    printk(KERN_INFO "=== GESTURE DRIVER LOADED ===\n");
    return 0;
}

static void __exit gesture_driver_exit(void)
{
    printk(KERN_INFO "=== GESTURE DRIVER EXIT ===\n");
    
    if (gesture_test_device) {
        platform_device_unregister(gesture_test_device);
    }
    
    platform_driver_unregister(&gesture_platform_driver);
    
    printk(KERN_INFO "=== GESTURE DRIVER UNLOADED ===\n");
}

module_init(gesture_driver_init);
module_exit(gesture_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ekaterina Paramonova");
MODULE_DESCRIPTION("Touchpad Gesture Device Driver");
MODULE_ALIAS("platform:touchpad_gesture");