#include <linux/module.h>
#include <linux/wmi.h>


#define NUC_WMI_GUID		"8C5DA44C-CDC3-46B3-8619-4E26D34390B7"

static int nuc_wmi_probe(struct wmi_device *wdev, const void *context)
{
	if (wdev == NULL)
		return -EINVAL;

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