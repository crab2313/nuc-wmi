#include <linux/module.h>
#include <linux/wmi.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>


#define NUC_WMI_GUID		"8C5DA44C-CDC3-46B3-8619-4E26D34390B7"

#define NUC_ACPI_PROC_DIR "nuc"
#define NUC_LED_ACPI_PROC_DIR "led"

/* Intel NUC WMI interface method definition */
#define NUC_METHODID_OLD_GET_LED                          0x1
#define NUC_METHODID_OLD_SET_LED                          0x2
#define NUC_METHODID_QUERY_LED                            0x3
#define NUC_METHODID_GET_LED                              0x4
#define NUC_METHODID_SET_INDICATOR                        0x5
#define NUC_METHODID_SET_CONTROL_ITEM                     0x6
#define NUC_METHODID_NOTIFICATION                         0x7

static int nuc_wmi_evalute_method(u32 method_id, const struct acpi_buffer *in,
		struct acpi_buffer *out)
{
	return wmi_evaluate_method(NUC_WMI_GUID, 0, method_id, in, out);
}

#define NUC_LED_TYPE_POWER				0x0
#define NUC_LED_TYPE_HDD				0x1
#define NUC_LED_TYPE_SKULL				0x2
#define NUC_LED_TYPE_EYES				0x3
#define NUC_LED_TYPE_FRONT_1				0x4
#define NUC_LED_TYPE_FRONT_2				0x5
#define NUC_LED_TYPE_FRONT_3				0x6
#define NUC_LED_TYPE_MAX				0x7

const char *led_types[] = {
	[NUC_LED_TYPE_POWER] = 		"power",
	[NUC_LED_TYPE_HDD] = 		"hdd",
	[NUC_LED_TYPE_SKULL] = 		"skull",
	[NUC_LED_TYPE_EYES] = 		"eyes",
	[NUC_LED_TYPE_FRONT_1] = 	"front1",
	[NUC_LED_TYPE_FRONT_2] = 	"front2",
	[NUC_LED_TYPE_FRONT_3] = 	"front3",
};

enum led_color {
	NUC_LED_COLOR_BLUE_AMBER = 0x0,
	NUC_LED_COLOR_BLUE_WHITE,
	NUC_LED_COLOR_RGB,
};

const char *led_colors[] = {
	[NUC_LED_COLOR_BLUE_AMBER] = "blue/amber",
	[NUC_LED_COLOR_BLUE_WHITE] = "blue/white",
	[NUC_LED_COLOR_RGB] = "rgb",
};

enum led_indicator {
	NUC_LED_INDICATOR_POWER_STATE = 0x0,
	NUC_LED_INDICATOR_HDD,
	NUC_LED_INDICATOR_ETHERNET,
	NUC_LED_INDICATOR_WIFI,
	NUC_LED_INDICATOR_SOFTWARE,
	NUC_LED_INDICATOR_POWER_LIMIT,
	/* Do not query supported items of DISABLE indicator */
	NUC_LED_INDICATOR_DISABLE,
};

const char *led_indicators[] = {
	[NUC_LED_INDICATOR_POWER_STATE] = "power_state",
	[NUC_LED_INDICATOR_HDD] = "hdd",
	[NUC_LED_INDICATOR_ETHERNET] = "ethernet",
	[NUC_LED_INDICATOR_WIFI] = "wifi",
	[NUC_LED_INDICATOR_SOFTWARE] = "software",
	[NUC_LED_INDICATOR_POWER_LIMIT] = "power_limit",
	[NUC_LED_INDICATOR_DISABLE] = "disable",
};

#define SINGLE_OR_RGB(prefix) \
	prefix##_COLOR, \
	prefix##_R = prefix##_COLOR, \
	prefix##_G, \
	prefix##_B

#define POWER_STATE_ITEMS(state) \
	state##_BRIGHTNESS, \
	state##_BLINKING_BEHAVIOR, \
	state##_BLINKING_FREQUENCY, \
	SINGLE_OR_RGB(state)

enum power_state_item {
	POWER_STATE_ITEMS(PS_S0),
	POWER_STATE_ITEMS(PS_S3),
	POWER_STATE_ITEMS(PS_READY),
	POWER_STATE_ITEMS(PS_S5),
};

enum hdd_item {
	HDD_BRIGHTNESS,
	SINGLE_OR_RGB(HDD),
	HDD_BEHAVIOR,
};

enum ethernet_item {
	ETHERNET_TYPE,
	ETHERNET_BRIGHTNESS,
	SINGLE_OR_RGB(ETHERNET),
};

enum wifi_item {
	WIFI_BRIGHTNESS,
	SINGLE_OR_RGB(WIFI),
};

enum software_item {
	SOFTWARE_BRIGHTNESS,
	SOFTWARE_BLINKING_BEHAVIOR,
	SOFTWARE_BLINKING_FREQUENCY,
	SINGLE_OR_RGB(SOFTWARE),
};

enum power_limit_item {
	PL_SCHEME,
	PL_BRIGHTNESS,
	SINGLE_OR_RGB(PL),
};

struct acpi_args {
	u8 arg0;
	u8 arg1;
	u8 arg2;
	u8 arg3;
} __packed;

static int nuc_wmi_query(u32 method, const struct acpi_args *args, u32 *output)
{
	struct acpi_buffer in = { sizeof(args), (void *)args};
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	u32 res, ret = 0;

	nuc_wmi_evalute_method(method, &in, &out);

	obj = (union acpi_object *)out.pointer;

	if (!obj)
		return -EINVAL;

	if (obj->type != ACPI_TYPE_BUFFER) {
		ret = -EINVAL;
		goto out_free;
	}

	res = *(u32 *)obj->buffer.pointer;

	if (res & 0xf) {
		pr_err("nuc query (%d, %d, %d) failed: %d\n", args->arg0,
				args->arg1, args->arg2, res & 0xf);
		ret = -EIO;
		goto out_free;
	}

	*output = res >> 8;

out_free:
	kfree(obj);
	return ret;
}

static int nuc_query_supported_types(u32 *types)
{
	struct acpi_args args = { .arg0 = 0 };
	return nuc_wmi_query(NUC_METHODID_QUERY_LED, &args, types);
}

static int nuc_query_supported_indicators(u8 type, u32 *indicators)
{
	struct acpi_args args = { .arg0 = 2, .arg1 = type };
	return nuc_wmi_query(NUC_METHODID_QUERY_LED, &args, indicators);
}

static int nuc_query_supported_items(u8 type, u8 indicator, u32 *items)
{
	struct acpi_args args = { .arg0 = 3, .arg1 = type, .arg2 = indicator};
	return nuc_wmi_query(NUC_METHODID_QUERY_LED, &args, items);
}

static int nuc_query_color_type(u8 type, enum led_color *color)
{
	u32 out = 0, ret;
	struct acpi_args args = { .arg0 = 1, .arg1 = type };
	ret = nuc_wmi_query(NUC_METHODID_QUERY_LED, &args, &out);
	*color = (enum led_color)(ffs(out)-1);
	return ret;
}

static int nuc_query_led_indicator(u8 type, enum led_indicator *indicator)
{
	u32 out = 0, ret;
	struct acpi_args args = { .arg0 = 0, .arg1 = type };
	ret = nuc_wmi_query(NUC_METHODID_GET_LED, &args, &out);
	*indicator = (enum led_indicator)out;
	return ret;
}

static int nuc_query_indicator_item(u8 type, enum led_indicator indicator,
		u8 item, u8 *value)
{
	u32 out = 0, ret;
	struct acpi_args args = {
		.arg0 = 1,
		.arg1 = type,
		.arg2 = (u8)indicator,
		.arg3 = (u8)item
	};
	ret = nuc_wmi_query(NUC_METHODID_GET_LED, &args, &out);
	*value = (u8)out;
	return ret;
}

struct nuc_led {
	bool valid;
	u8 type;
	enum led_color color;
	enum led_indicator indicator;
	u8 allowed_indicator;
	u32 allowed_indicator_item[8];
};

struct nuc_wmi {
	struct nuc_led leds[NUC_LED_TYPE_MAX];
};

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *led_dir;

static void color_formatter(struct seq_file *m, struct nuc_led *led,
	u8 indicator, u8 base)
{
	u8 c1, c2, c3;

	nuc_query_indicator_item(led->type, indicator, base, &c1);

	if (led->color == NUC_LED_COLOR_RGB) {
		nuc_query_indicator_item(led->type, indicator, base+1, &c2);
		nuc_query_indicator_item(led->type, indicator, base+2, &c3);
		seq_printf(m, "rgb(%d,%d,%d)", c1, c2, c3);
	} else {
		seq_printf(m, "%s", !c1 ? "blue" :
			led->color == NUC_LED_COLOR_BLUE_AMBER ?
			"amber" : "white");
	}
}

static void brightness_formatter(struct seq_file *m, struct nuc_led *led,
	u8 indicator, u8 base)
{
	u8 brightness;

	nuc_query_indicator_item(led->type, indicator, base, &brightness);
	seq_printf(m, "brightness %d%%", brightness * 100 / 0x64);
}

static void blinking_formatter(struct seq_file *m, struct nuc_led *led,
	u8 indicator, u8 base)
{
	u8 behavior, freq;
	const char *blink[] = {
		"solid", "breathing", "pulsing", "strobing",
	};

	nuc_query_indicator_item(led->type, indicator, base, &behavior);
	nuc_query_indicator_item(led->type, indicator, base + 1, &freq);
	seq_printf(m, "%s %d/12Hz",
		behavior < ARRAY_SIZE(blink) ? blink[behavior] : "unknown",
		freq);
}

static void brightness_color_formatter(struct seq_file *m, struct nuc_led *led,
	u8 indicator, u8 base)
{
	brightness_formatter(m, led, indicator, base);
	seq_putc(m, ' ');
	color_formatter(m, led, indicator, base + 1);
}

static void brightness_blinking_color_formatter(struct seq_file *m,
	struct nuc_led *led, u8 indicator, u8 base)
{
	brightness_formatter(m, led, indicator, base);
	seq_putc(m, ' ');
	blinking_formatter(m, led, indicator, base + 1);
	seq_putc(m, ' ');
	color_formatter(m, led, indicator, base + 3);
}

static void power_state_formatter(struct seq_file *m, struct nuc_led *led)
{
	const char *blink[] = {
		"solid", "breathing", "pulsing", "strobing",
	};

#define PRINT_STATE(state) \
	do {			\
		u8 value;	\
		seq_printf(m, "%s:", #state);				\
		nuc_query_indicator_item(led->type, 			\
			NUC_LED_INDICATOR_POWER_STATE, 			\
			PS_##state##_BRIGHTNESS, &value);		\
		seq_printf(m, " brightness %d%%,", value * 100 / 0x64); \
		nuc_query_indicator_item(led->type, 			\
			NUC_LED_INDICATOR_POWER_STATE, 			\
			PS_##state##_BLINKING_BEHAVIOR, &value);	\
		seq_printf(m, " %s", blink[value]);			\
		nuc_query_indicator_item(led->type, 			\
			NUC_LED_INDICATOR_POWER_STATE, 			\
			PS_##state##_BLINKING_FREQUENCY, &value);	\
		seq_printf(m, " %d/12Hz,", value);			\
		seq_putc(m, '\n');					\
	} while(0);

	PRINT_STATE(S0);
	PRINT_STATE(S3);
	PRINT_STATE(READY);
	PRINT_STATE(S5);

#undef PRINT_STATE
}

static void hdd_formatter(struct seq_file *m, struct nuc_led *led)
{
	brightness_formatter(m, led, NUC_LED_INDICATOR_HDD, HDD_BRIGHTNESS);
	seq_putc(m, ' ');
	color_formatter(m, led, NUC_LED_INDICATOR_HDD, HDD_COLOR);
}

static void ethernet_formatter(struct seq_file *m, struct nuc_led *led)
{
	u8 type;

	nuc_query_indicator_item(led->type, NUC_LED_INDICATOR_ETHERNET,
		ETHERNET_TYPE, &type);
	seq_printf(m, "%s ", type == 0 ? "LAN1" :
			type == 1 ? "LAN2" :
			type == 2 ? "LAN1+LAN2" : "UNKNOWN");
	brightness_color_formatter(m, led, NUC_LED_INDICATOR_ETHERNET,
		ETHERNET_BRIGHTNESS);
}

static void wifi_formatter(struct seq_file *m, struct nuc_led *led)
{
	brightness_color_formatter(m, led, NUC_LED_INDICATOR_WIFI,
		WIFI_BRIGHTNESS);
}

static void software_formatter(struct seq_file *m, struct nuc_led *led)
{
	brightness_blinking_color_formatter(m, led, NUC_LED_INDICATOR_SOFTWARE,
		SOFTWARE_BRIGHTNESS);
}

static void power_limit_formatter(struct seq_file *m, struct nuc_led *led)
{
	u8 scheme;

	nuc_query_indicator_item(led->type, NUC_LED_INDICATOR_POWER_LIMIT,
		PL_SCHEME, &scheme);

	switch (scheme) {
		case 0:
			seq_printf(m, "green-to-red ");
			brightness_color_formatter(m, led,
				NUC_LED_INDICATOR_POWER_LIMIT, PL_BRIGHTNESS);
			break;
		case 1:
			seq_printf(m, "single-color ");
			color_formatter(m, led,
				NUC_LED_INDICATOR_POWER_LIMIT, PL_COLOR);
			break;
		default:
			seq_printf(m, "unknown power limit scheme");
	}
}

typedef void (*item_format_func)(struct seq_file *, struct nuc_led *);

static item_format_func item_formatter[] = {
	[NUC_LED_INDICATOR_POWER_STATE] = power_state_formatter,
	[NUC_LED_INDICATOR_HDD] = hdd_formatter,
	[NUC_LED_INDICATOR_ETHERNET] = ethernet_formatter,
	[NUC_LED_INDICATOR_WIFI] = wifi_formatter,
	[NUC_LED_INDICATOR_SOFTWARE] = software_formatter,
	[NUC_LED_INDICATOR_POWER_LIMIT] = power_limit_formatter,
};

static int nuc_led_proc_show(struct seq_file *m, void *v)
{
	int i;
	struct nuc_led *led = (struct nuc_led *)m->private;

	if (!m || !led)
		return -EINVAL;
	seq_printf(m, "type:\t\t%s\n", led_types[led->type]);
	seq_printf(m, "color:\t\t%s\n", led_colors[led->color]);
	seq_printf(m, "indicator:\t%s\n", led_indicators[led->indicator]);

	if (led->indicator < ARRAY_SIZE(item_formatter)
		&& item_formatter[led->indicator])
		item_formatter[led->indicator](m, led);

	seq_putc(m, '\n');

	seq_printf(m, "allowed indicators:");
	for (i = 0; i < NUC_LED_INDICATOR_DISABLE; i++)
		if (led->allowed_indicator & (1 << i))
			seq_printf(m, " %s", led_indicators[i]);
	seq_putc(m, '\n');

	return 0;
}

static int nuc_led_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nuc_led_proc_show, PDE_DATA(inode));
}

static const struct proc_ops led_proc_ops = {
	.proc_open = nuc_led_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int nuc_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct nuc_wmi *data;
	struct nuc_led *led;
	u32 types, indicators, items;
	int i, j, ret;

	if (wdev == NULL)
		return -EINVAL;

	data = devm_kzalloc(&wdev->dev, sizeof(struct nuc_wmi), GFP_KERNEL);

	if (data == NULL)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);

	if (nuc_query_supported_types(&types) < 0)
		return -EIO;

	for (i = 0; i < NUC_LED_TYPE_MAX; i++) {
		if (!(types & (1 << i)))
			continue;

		dev_info(&wdev->dev, "supported LED type: %s\n", led_types[i]);
		led = &data->leds[i];
		led->valid = true;
		led->type = i;

		if (nuc_query_color_type(i, &led->color) < 0)
			return -EIO;

		dev_info(&wdev->dev, "color type %d\n", led->color);

		if (nuc_query_supported_indicators(i, &indicators) < 0)
			return -EIO;

		dev_info(&wdev->dev, "supported indicators: %x\n", indicators);
		led->allowed_indicator = indicators;

		if (nuc_query_led_indicator(i, &led->indicator) < 0)
			return -EIO;

		dev_info(&wdev->dev, "current indicator %s\n",
				led_indicators[led->indicator]);

		for (j = 0; j < NUC_LED_INDICATOR_DISABLE; j++) {
			if (!(indicators & (1 << j)))
				continue;

			ret = nuc_query_supported_items(led->type, j, &items);

			if (ret < 0)
				return -EIO;

			led->allowed_indicator_item[j] = items;
		}
	}

	proc_dir = proc_mkdir(NUC_ACPI_PROC_DIR, acpi_root_dir);
	if (!proc_dir) {
		pr_err("unable to create proc dir " NUC_ACPI_PROC_DIR "\n");
		return -ENODEV;
	}

	led_dir = proc_mkdir(NUC_LED_ACPI_PROC_DIR, proc_dir);

	if (!led_dir) {
		pr_err("unable to create proc led dir" NUC_LED_ACPI_PROC_DIR "\n");
		return -ENODEV;
	}

	for (i = 0; i < NUC_LED_TYPE_MAX; i++) {
		if (!data->leds[i].valid)
			continue;
		if (!proc_create_data(led_types[i], S_IRUGO | S_IWUSR, led_dir,
			&led_proc_ops, &data->leds[i])) {
                        return -ENODEV;
		}
	}

	return 0;
}

static int nuc_wmi_remove(struct wmi_device *wdev)
{
	if (led_dir)
		proc_remove(led_dir);
	if (proc_dir)
		proc_remove(proc_dir);
        return 0;
}

static struct wmi_device_id nuc_wmi_id_table[] = {
	{ NUC_WMI_GUID, NULL },

	/* Terminating entry */
	{ },
};

static struct wmi_driver nuc_wmi = {
	.driver = {
		.name = "nuc-wmi",
	},
	.id_table = nuc_wmi_id_table,
	.probe = nuc_wmi_probe,
	.remove = nuc_wmi_remove,
};
module_wmi_driver(nuc_wmi);

MODULE_DEVICE_TABLE(wmi, nuc_wmi_id_table);
MODULE_AUTHOR("Qiu Wenbo <crab2313@gmail.com>");
MODULE_DESCRIPTION("WMI driver for Intel NUC kit");
MODULE_LICENSE("GPL v2");