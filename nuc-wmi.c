#include <linux/module.h>
#include <linux/wmi.h>


#define NUC_WMI_GUID		"8C5DA44C-CDC3-46B3-8619-4E26D34390B7"

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
	NUC_LED_COLOR_TYPE_BLUE_AMBER = 0x0,
	NUC_LED_COLOR_TYPE_BLUE_WHITE,
	NUC_LED_COLOR_TYPE_RGB,
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

struct acpi_args {
	u8 arg0;
	u8 arg1;
	u8 arg2;
	u8 arg3;
} __packed;

static int nuc_query_leds(const struct acpi_args *args, u32 *bitmap)
{
	struct acpi_buffer in = { sizeof(args), (void *)args};
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	int ret = 0;
	u32 res;

	nuc_wmi_evalute_method(NUC_METHODID_QUERY_LED, &in, &out);

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
		ret = -EINVAL;
		goto out_free;
	}

	*bitmap = res >> 8;

out_free:
	kfree(obj);
	return ret;
}

static int nuc_query_supported_types(u32 *types)
{
	struct acpi_args args = { .arg0 = 0 };
	return nuc_query_leds(&args, types);
}

static int nuc_query_supported_indicators(u8 type, u32 *indicators)
{
	struct acpi_args args = { .arg0 = 2, .arg1 = type };
	return nuc_query_leds(&args, indicators);
}

static int nuc_query_supported_items(u8 type, u8 indicator, u32 *items)
{
	struct acpi_args args = { .arg0 = 3, .arg1 = type, .arg2 = indicator};
	return nuc_query_leds(&args, items);
}

static int nuc_query_color_type(u8 type, enum led_color *color)
{
	u32 tmp = 0;
	struct acpi_args args = { .arg0 = 1, .arg1 = type };
	return nuc_query_leds(&args, &tmp);
	*color = (enum led_color)tmp;
}

struct nuc_led {
	bool valid;
	u8 type;
	enum led_color color;
	u8 indicator, indicator_item;
	u8 allowed_indicator;
	u32 allowed_indicator_item[8];
};

struct nuc_wmi {
	struct nuc_led leds[NUC_LED_TYPE_MAX];
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


		for (j = 0; j < NUC_LED_INDICATOR_DISABLE; j++) {
			if (!(indicators & (1 << j)))
				continue;

			ret = nuc_query_supported_items(led->type, j, &items);

			if (ret < 0)
				return -EIO;

			led->allowed_indicator_item[j] = items;
		}
	}

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
};
module_wmi_driver(nuc_wmi);

MODULE_DEVICE_TABLE(wmi, nuc_wmi_id_table);
MODULE_AUTHOR("Qiu Wenbo <crab2313@gmail.com>");
MODULE_DESCRIPTION("WMI driver for Intel NUC kit");
MODULE_LICENSE("GPL v2");