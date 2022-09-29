/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "context.h"
#include "network.h"
#include "vhci.h"
#include "device_ioctl.h"

#include <libdrv\dbgcommon.h>

namespace
{

using namespace usbip;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UDECXUSBDEVICE, get_workitem_ctx);

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE auto to_udex_speed(_In_ usb_device_speed speed)
{
        PAGED_CODE();

        switch (speed) {
        case USB_SPEED_SUPER_PLUS:
        case USB_SPEED_SUPER:
                return UdecxUsbSuperSpeed;
        case USB_SPEED_WIRELESS:
        case USB_SPEED_HIGH:
                return UdecxUsbHighSpeed;
        case USB_SPEED_FULL:
                return UdecxUsbFullSpeed;
        case USB_SPEED_LOW:
        case USB_SPEED_UNKNOWN:
        default:
                return UdecxUsbLowSpeed;
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI device_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto dev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("dev %04x", ptr04x(dev));

        if (auto ctx = get_device_ctx(dev)) {
                free(ctx->ext);
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void device_cleanup(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto dev = static_cast<UDECXUSBDEVICE>(Object);
        auto &ctx = *get_device_ctx(dev);

        TraceDbg("dev %04x", ptr04x(dev));

        vhci::forget_device(dev);
        close_socket(ctx.ext->sock);
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_RESET)
_IRQL_requires_same_
void endpoint_reset([[maybe_unused]]_In_ UDECXUSBENDPOINT endp, _In_ WDFREQUEST Request)
{
        TraceDbg("\n"); 
        WdfRequestComplete(Request, STATUS_SUCCESS);
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_ENTRY)
_IRQL_requires_same_
NTSTATUS device_d0_entry(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev)
{
        TraceDbg("vhci %04x, dev %04x", ptr04x(vhci), ptr04x(dev));
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_EXIT)
_IRQL_requires_same_
NTSTATUS device_d0_exit(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev, _In_ UDECX_USB_DEVICE_WAKE_SETTING WakeSetting)
{
        TraceDbg("vhci %04x, dev %04x, %!UDECX_USB_DEVICE_WAKE_SETTING!", ptr04x(vhci), ptr04x(dev), WakeSetting);
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_SET_FUNCTION_SUSPEND_AND_WAKE)
_IRQL_requires_same_
NTSTATUS device_set_function_suspend_and_wake(
        _In_ WDFDEVICE vhci, 
        _In_ UDECXUSBDEVICE dev, 
        _In_ ULONG Interface, 
        _In_ UDECX_USB_DEVICE_FUNCTION_POWER FunctionPower)
{
        TraceDbg("vhci %04x, dev %04x, Interface %lu, %!UDECX_USB_DEVICE_FUNCTION_POWER!", 
                ptr04x(vhci), ptr04x(dev), Interface, FunctionPower);

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto create_queue(_In_ UDECXUSBENDPOINT endp)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchParallel);
        cfg.EvtIoInternalDeviceControl = usbip::device::internal_device_control;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, UDECXUSBENDPOINT);
        attrs.ParentObject = endp;

        auto &ctx = *get_endpoint_ctx(endp);
        auto &dev_ctx = *get_device_ctx(ctx.device);

        WDFQUEUE queue; // save to endpoint_ctx if it requires
        if (auto err = WdfIoQueueCreate(dev_ctx.vhci, &cfg, &attrs, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        *get_queue_ctx(queue) = endp;
        UdecxUsbEndpointSetWdfIoQueue(endp, queue); // PASSIVE_LEVEL

        TraceDbg("dev %04x, endp %04x -> queue %04x", ptr04x(ctx.device), ptr04x(endp), ptr04x(queue));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto create_endpoint(
        _Out_ UDECXUSBENDPOINT &result, _In_ UDECXUSBDEVICE dev, _In_ _UDECXUSBENDPOINT_INIT *init, 
        _In_ UCHAR EndpointAddress, _In_ EVT_UDECX_USB_ENDPOINT_RESET *EvtUsbEndpointReset)
{
        PAGED_CODE();

        UdecxUsbEndpointInitSetEndpointAddress(init, EndpointAddress);

        UDECX_USB_ENDPOINT_CALLBACKS cb;
        UDECX_USB_ENDPOINT_CALLBACKS_INIT(&cb, EvtUsbEndpointReset);
        UdecxUsbEndpointInitSetCallbacks(init, &cb);

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, endpoint_ctx);
        attrs.ParentObject = dev;

        if (auto err = UdecxUsbEndpointCreate(&init, &attrs, &result)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbEndpointCreate %!STATUS!", err);
                return err;
        }

        if (auto ctx = get_endpoint_ctx(result)) {
                ctx->device = dev;
        }

        if (auto err = create_queue(result)) {
                return err;
        }

        TraceDbg("dev %04x -> endp %04x, addr %#x", ptr04x(dev), ptr04x(result), EndpointAddress);
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS default_endpoint_add(_In_ UDECXUSBDEVICE dev, _In_ _UDECXUSBENDPOINT_INIT *init)
{
        PAGED_CODE();

        auto &ctx = *get_device_ctx(dev);
        TraceDbg("dev %04x", ptr04x(dev));

        return create_endpoint(ctx.ep0, dev, init, USB_DEFAULT_DEVICE_ADDRESS, endpoint_reset);
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINT_ADD)
_IRQL_requires_same_
NTSTATUS endpoint_add(_In_ UDECXUSBDEVICE dev, [[maybe_unused]] _In_ UDECX_USB_ENDPOINT_INIT_AND_METADATA *EndpointToCreate)
{
        TraceDbg("dev %04x", ptr04x(dev));
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(_In_ UDECXUSBDEVICE dev, _In_ WDFREQUEST Request, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *Params)
{
        TraceDbg("dev %04x, EndpointsToConfigureCount %lu", ptr04x(dev), Params->EndpointsToConfigureCount);
        WdfRequestComplete(Request, STATUS_SUCCESS);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto create_init(_In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        auto init = UdecxUsbDeviceInitAllocate(vhci);
        if (!init) {
                return init;
        }

        UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS cb;
        UDECX_USB_DEVICE_CALLBACKS_INIT(&cb);

        cb.EvtUsbDeviceLinkPowerEntry = device_d0_entry;
        cb.EvtUsbDeviceLinkPowerExit = device_d0_exit;

        cb.EvtUsbDeviceSetFunctionSuspendAndWake = device_set_function_suspend_and_wake; // required for USB 3 devices
//      cb.EvtUsbDeviceReset = nullptr;

        cb.EvtUsbDeviceDefaultEndpointAdd = default_endpoint_add;
        cb.EvtUsbDeviceEndpointAdd = endpoint_add;
        cb.EvtUsbDeviceEndpointsConfigure = endpoints_configure;

        UdecxUsbDeviceInitSetStateChangeCallbacks(init, &cb);

        UdecxUsbDeviceInitSetSpeed(init, speed);
        UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);

        return init;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &dev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext)
{
        PAGED_CODE();

        NT_ASSERT(ext);
        auto speed = to_udex_speed(ext->dev.speed);

        auto init = create_init(vhci, speed); // must be freed if UdecxUsbDeviceCreate fails
        if (!init) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAllocate error");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, device_ctx);
        attrs.EvtCleanupCallback = device_cleanup;
        attrs.EvtDestroyCallback = device_destroy;
//      attrs.ParentObject = vhci; // FIXME: by default?

        if (auto err = UdecxUsbDeviceCreate(&init, &attrs, &dev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                UdecxUsbDeviceInitFree(init); // must never be called if success, Udecx does that itself
                return err;
        }

        if (auto ctx = get_device_ctx(dev)) {
                ctx->vhci = vhci;
                ctx->ext = ext;
                ext->ctx = ctx;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x", ptr04x(dev));
        return STATUS_SUCCESS;
}

/*
 * UDECXUSBDEVICE must be destroyed in two steps:
 * 1.Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 *   A device will be plugged out from a hub, but not destroyed.
 * 2.Call WdfObjectDelete to destroy it.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void usbip::device::destroy(_In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();

        auto &ctx = *get_device_ctx(dev);
        static_assert(sizeof(ctx.destroyed) == sizeof(CHAR));

        if (InterlockedExchange8(reinterpret_cast<CHAR*>(&ctx.destroyed), true)) {
                TraceDbg("dev %04x was already destroyed, port %d", ptr04x(dev), ctx.port);
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x, port %d", ptr04x(dev), ctx.port);

        if (auto err = UdecxUsbDevicePlugOutAndDelete(dev)) { // PASSIVE_LEVEL
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugOutAndDelete(dev=%04x) %!STATUS!", ptr04x(dev), err);
        }

        WdfObjectDelete(dev);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::schedule_destroy(_In_ UDECXUSBDEVICE dev)
{
        auto func = [] (auto WorkItem)
        {
                if (auto dev = *get_workitem_ctx(WorkItem)) {
                        destroy(dev);
                        WdfObjectDereference(dev);
                }
                WdfObjectDelete(WorkItem); // can be omitted
        };

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, func);

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, UDECXUSBDEVICE);
        attrs.ParentObject = dev;

        WDFWORKITEM wi{};
        if (auto err = WdfWorkItemCreate(&cfg, &attrs, &wi)) {
                Trace(TRACE_LEVEL_ERROR, "WdfWorkItemCreate %!STATUS!", err);
                return err;
        }

        *get_workitem_ctx(wi) = dev;
        WdfObjectReference(dev);

        WdfWorkItemEnqueue(wi);

        static_assert(NT_SUCCESS(STATUS_PENDING));
        return STATUS_PENDING;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
UDECXUSBDEVICE usbip::device::get_device(_In_ WDFQUEUE queue)
{
        auto endp = *get_queue_ctx(queue);
        auto ctx = get_endpoint_ctx(endp);
        return ctx->device;
}

