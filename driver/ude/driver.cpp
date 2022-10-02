/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "driver.h"
#include "vhci.h"
#include "trace.h"
#include "driver.tmh"

#include "context.h"
#include "wsk_context.h"

#include <libdrv\wsk_cpp.h>

namespace
{

using namespace usbip;

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void driver_cleanup(_In_ WDFOBJECT Object)
{
	PAGED_CODE();

	auto drv = static_cast<WDFDRIVER>(Object);
	Trace(TRACE_LEVEL_INFORMATION, "driver %04x", ptr04x(drv));

	wsk::shutdown();
	delete_wsk_context_list();

	auto drvobj = WdfDriverWdmGetDriverObject(drv);
	WPP_CLEANUP(drvobj);
}

/*
 * Configure Inflight Trace Recorder (IFR) parameter "VerboseOn".
 * The default setting of zero causes the IFR to log errors, warnings, and informational events.
 * Set to one to add verbose output to the log.
 *
 * reg add "HKLM\SYSTEM\ControlSet001\Services\usbip2_vhci\Parameters" /v VerboseOn /t REG_DWORD /d 1 /f
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
CS_INIT auto set_ifr_verbose()
{
	PAGED_CODE();

	WDFKEY key;
	if (auto err = WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &key)) {
		return err;
	}

	DECLARE_CONST_UNICODE_STRING(name, L"VerboseOn");
	ULONG value;

	auto err = WdfRegistryQueryULong(key, &name, &value);
	if (err == STATUS_OBJECT_NAME_NOT_FOUND) { // set if value does not exist
		err = WdfRegistryAssignULong(key, &name, 1);
	}

	WdfRegistryClose(key);
	return err;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT auto driver_create(_In_ DRIVER_OBJECT *DriverObject, _In_ UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	WDF_OBJECT_ATTRIBUTES attrs;
	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.EvtCleanupCallback = driver_cleanup;

	WDF_DRIVER_CONFIG cfg;
	WDF_DRIVER_CONFIG_INIT(&cfg, DriverDeviceAdd);
	cfg.DriverPoolTag = POOL_TAG;

	return WdfDriverCreate(DriverObject, RegistryPath, &attrs, &cfg, nullptr);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT auto init()
{
	PAGED_CODE();

	if (auto err = init_wsk_context_list(POOL_TAG)) {
		Trace(TRACE_LEVEL_CRITICAL, "ExInitializeLookasideListEx %!STATUS!", err);
		return err;
	}

	if (auto err = wsk::initialize()) {
		Trace(TRACE_LEVEL_CRITICAL, "WskRegister %!STATUS!", err);
		return err;
	}

	return STATUS_SUCCESS;
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *DriverObject, _In_ UNICODE_STRING *RegistryPath)
{
        PAGED_CODE();

	if (auto err = driver_create(DriverObject, RegistryPath)) {
		return err;
        } else {
		err = set_ifr_verbose();
		WPP_INIT_TRACING(DriverObject, RegistryPath);
		if (err) {
			Trace(TRACE_LEVEL_ERROR, "set_ifr_verbose %!STATUS!", err);
		}
	}

	Trace(TRACE_LEVEL_INFORMATION, "RegistryPath '%!USTR!'", RegistryPath);
	return init();
}
