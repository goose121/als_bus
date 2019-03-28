// This file is part of als_bus.
//
// als_bus is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// als_bus is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with alsd. If not, see <https://www.gnu.org/licenses/>.

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/acpi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("https://github.com/user/goose121");
MODULE_DESCRIPTION("ACPI-bus-based driver for ambient light sensor devices");
MODULE_VERSION("0.0.1");

ACPI_MODULE_NAME("als_bus");

static ssize_t als_bus_show_illuminance(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	struct acpi_device *adev = to_acpi_device(dev);
	unsigned long long ali_reading;
	acpi_status status;

	status = acpi_evaluate_integer(adev->handle, "_ALI", NULL, &ali_reading);

	if(ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Error reading ALS illuminance"));
		return -EIO;
	}
	
	return snprintf(buf, PAGE_SIZE, "%llu", ali_reading);
}

static int als_bus_snprintf(char **buf,
			    size_t *remaining_space,
			    const char *fmt, ...) {
	
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(*buf, *remaining_space, fmt, args);
	va_end(args);

	*buf += len;
	*remaining_space -= len;
	return len;
}

static ssize_t als_bus_show_response(struct device *dev,
				     struct device_attribute *attr,
				     char *buf) {
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *result;
	union acpi_object *package;
	size_t buf_remaining = PAGE_SIZE;
	acpi_status status;
	int i;

	status = acpi_evaluate_object(adev->handle, "_ALR", NULL, &buffer);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Error reading ALS response table"));
		return -EIO;
	}

	result = buffer.pointer;

	als_bus_snprintf(&buf, &buf_remaining, "[");
	for (i = 0; i < result->package.count; ++i) {
		if (i > 0) {
			als_bus_snprintf(&buf, &buf_remaining, ", ");
		}

		package = &result->package.elements[i];

		als_bus_snprintf(
			&buf,
			&buf_remaining,
			"[%u, %u]",
		        package->package.elements[0].integer.value,
			package->package.elements[1].integer.value);
	}
	als_bus_snprintf(&buf, &buf_remaining, "]");
	
	return PAGE_SIZE - buf_remaining;
}

static DEVICE_ATTR(ali, 0444, als_bus_show_illuminance, NULL);
static DEVICE_ATTR(alr, 0444, als_bus_show_response, NULL);

static struct attribute *als_bus_attributes[] = {
	&dev_attr_ali.attr,
	&dev_attr_alr.attr,
	NULL
};

static const struct attribute_group als_bus_attribute_group = {
	.name = "als_bus",
	.attrs = als_bus_attributes
};

static int als_bus_add(struct acpi_device *adev) {
	int res;

	res = devm_device_add_group(&adev->dev, &als_bus_attribute_group);
	if (res) {
		printk(KERN_ERR "Error creating sysfs group: %d\n", res);
		sysfs_remove_group(&adev->dev.kobj, &als_bus_attribute_group);
		return -ENODEV;
	}

	return 0;
}

static void als_bus_notify(struct acpi_device *adev, u32 event) {
	switch (event) {
	case 0x80:					/* Change in illuminance */
	case 0x81:					/* Change in colour temperature */
	case 0x82:					/* Change in response */
		acpi_bus_generate_netlink_event(
			adev->pnp.device_class,
			dev_name(&adev->dev),
			event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO
				  "Unexpected event code 0x%x", event));
		break;
	}
}

static const struct acpi_device_id als_bus_device_ids[] = {
	{"ACPI0008", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, als_bus_device_ids);

static struct acpi_driver als_bus_driver = {
	.name = "als_bus",
	.class = "als",
	.ids = als_bus_device_ids,
	.ops = {
		.add = als_bus_add,
		.notify = als_bus_notify,
	},
	.owner = THIS_MODULE,
};

module_acpi_driver(als_bus_driver);
