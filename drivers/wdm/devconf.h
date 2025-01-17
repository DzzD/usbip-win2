#pragma once

#include <wdm.h>
#include <usbdi.h>

#include <usbip\ch9.h>
#include <usbip\proto.h> 

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS setup_config(_URB_SELECT_CONFIGURATION *cfg, usb_device_speed speed);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS setup_intf(USBD_INTERFACE_INFORMATION *intf_info, usb_device_speed speed, USB_CONFIGURATION_DESCRIPTOR *cfgd);

enum { 
	SELECT_CONFIGURATION_STR_BUFSZ = 1024, 
	SELECT_INTERFACE_STR_BUFSZ = SELECT_CONFIGURATION_STR_BUFSZ 
};

_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_configuration_str(char *buf, size_t len, const _URB_SELECT_CONFIGURATION *cfg);

_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_interface_str(char *buf, size_t len, const _URB_SELECT_INTERFACE *iface);

/*
 * @return bEndpointAddress of endpoint descriptor 
 */
inline auto get_endpoint_address(USBD_PIPE_HANDLE handle)
{
	auto v = reinterpret_cast<UCHAR*>(&handle);
	return v[0];
}

inline auto get_endpoint_interval(USBD_PIPE_HANDLE handle)
{
	auto v = reinterpret_cast<UCHAR*>(&handle);
	return v[1];
}

inline auto get_endpoint_type(USBD_PIPE_HANDLE handle)
{
	auto v = reinterpret_cast<UCHAR*>(&handle);
	return static_cast<USBD_PIPE_TYPE>(v[2]);
}

inline UCHAR get_endpoint_number(USBD_PIPE_HANDLE handle)
{
	auto addr = get_endpoint_address(handle);
	return addr & USB_ENDPOINT_ADDRESS_MASK;
}

/*
 * EP0 is bidirectional. 
 */
inline bool is_endpoint_direction_in(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	auto addr = get_endpoint_address(handle);
	return USB_ENDPOINT_DIRECTION_IN(addr);
}

/*
* EP0 is bidirectional. 
*/
inline bool is_endpoint_direction_out(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	auto addr = get_endpoint_address(handle);
	return USB_ENDPOINT_DIRECTION_OUT(addr);
}

const USBD_PIPE_HANDLE EP0 = 0; // make_pipe_handle(USB_DEFAULT_ENDPOINT_ADDRESS, UsbdPipeTypeControl, 0);
static_assert(!USB_DEFAULT_ENDPOINT_ADDRESS);
static_assert(!UsbdPipeTypeControl);
/*
static_assert(!EP0);
static_assert(get_endpoint_address(EP0) == USB_DEFAULT_ENDPOINT_ADDRESS);
static_assert(get_endpoint_type(EP0) == UsbdPipeTypeControl);
static_assert(!get_endpoint_number(EP0));
static_assert(!get_endpoint_interval(EP0));
*/
