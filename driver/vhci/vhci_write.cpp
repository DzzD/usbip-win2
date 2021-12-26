#include "vhci.h"
#include "trace.h"
#include "vhci_write.tmh"

#include "dbgcommon.h"
#include "usbip_proto.h"
#include "vhci_vpdo.h"
#include "vhci_vpdo_dsc.h"
#include "usbreq.h"
#include "usbd_helper.h"
#include "vhci_irp.h"
#include "pdu.h"

/*
 * These URBs have the same layout from the beginning of the structure.
 * Pointer to any of them can be used to access TransferBufferLength, TransferBuffer, TransferBufferMDL of any other instance 
 * of these URBs.
 */
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferFlags), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferFlags), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_ISOCH_TRANSFER, TransferFlags), "assert");
//static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferFlags), "assert");
//static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferFlags), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferFlags), "assert");
//static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferFlags), "assert");
//static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferFlags), "assert");
//static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferFlags) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferFlags), "assert");

static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferLength), "assert");

static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBuffer), "assert");

static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferMDL), "assert");

#define TRANSFERRED(irp) ((irp)->IoStatus.Information)

static inline auto get_irp_buffer(const IRP *irp)
{
	return irp->AssociatedIrp.SystemBuffer;
}

static PAGEABLE ULONG get_irp_buffer_size(const IRP *irp)
{
	PAGED_CODE();
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation((IRP*)irp);
	return irpstack->Parameters.Write.Length;
}

static PAGEABLE void *get_urb_buffer(void *buf, MDL *bufMDL)
{
	PAGED_CODE();

	if (buf) {
		return buf;
	}

	if (!bufMDL) {
		TraceError(TRACE_WRITE, "TransferBuffer and TransferBufferMDL are NULL");
		return nullptr;
	}

	buf = MmGetSystemAddressForMdlSafe(bufMDL, NormalPagePriority | MdlMappingNoExecute);
	if (!buf) {
		TraceError(TRACE_WRITE, "MmGetSystemAddressForMdlSafe error");
	}

	return buf;
}

/*
 * Actual TransferBufferLength must already be set.
 */
static PAGEABLE void *copy_to_transfer_buffer(URB *urb, const void *src)
{
	PAGED_CODE();

	bool mdl = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
	NT_ASSERT(urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used
	void *buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);

	if (buf) {
		RtlCopyMemory(buf, src, r->TransferBufferLength);
	} else {
		r->TransferBufferLength = 0;
	}

	return buf;
}

static PAGEABLE NTSTATUS assign(ULONG *TransferBufferLength, int actual_length)
{
	PAGED_CODE();

	bool ok = actual_length >= 0 && (ULONG)actual_length <= *TransferBufferLength;
	*TransferBufferLength = ok ? actual_length : 0;

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * MmGetSystemAddressForMdlSafe is the most probable source of the failure of copy_to_transfer_buffer.
 */
static constexpr auto get_copy_status(const void *p)
{
	return p ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static PAGEABLE NTSTATUS urb_function_generic(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used
	NTSTATUS err = assign(&r->TransferBufferLength, hdr->u.ret_submit.actual_length);

	if (err || is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
		return err;
	}

	USHORT func = urb->UrbHeader.Function;

	bool bulk = func == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER || 
		    func == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;

	void *buf = copy_to_transfer_buffer(urb, hdr + 1);
	if (buf && !bulk) { // bulk can have sensitive data
		TraceInfo(TRACE_WRITE, "%s(%#04x): %!BIN!", urb_function_str(func), func, 
					WppBinary(buf, (USHORT)r->TransferBufferLength));
	}

	return get_copy_status(buf);
}

static PAGEABLE NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();
	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : vpdo_select_config(vpdo, &urb->UrbSelectConfiguration);
}

static PAGEABLE NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();
	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : vpdo_select_interface(vpdo, &urb->UrbSelectInterface);
}

/*
 * A request can read descriptor header or full descriptor to obtain its real size.
 * F.e. configuration descriptor is 9 bytes, but the full size is stored in wTotalLength.
 */
static PAGEABLE NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();

	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	NTSTATUS err = assign(&r->TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (err) {
		return err;
	}

	switch (r->Hdr.Function) {
	case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
		return STATUS_SUCCESS; // USB_DIR_OUT
	}

	const USB_COMMON_DESCRIPTOR *dsc = nullptr;

	if (r->TransferBufferLength < sizeof(*dsc)) {
		TraceError(TRACE_WRITE, "usb descriptor's header expected: TransferBufferLength(%lu)", r->TransferBufferLength);
		r->TransferBufferLength = 0;
		return STATUS_INVALID_PARAMETER;
	}

	dsc = (USB_COMMON_DESCRIPTOR*)copy_to_transfer_buffer(urb, hdr + 1);
	if (!dsc) {
		return get_copy_status(dsc);
	}

	TraceInfo(TRACE_WRITE, "%s: bLength %d, %!usb_descriptor_type!, %!BIN!", 
				urb_function_str(r->Hdr.Function), 
				dsc->bLength,
				dsc->bDescriptorType,
				WppBinary(dsc, (USHORT)r->TransferBufferLength));

	if (hdr->u.ret_submit.status) {
		return STATUS_UNSUCCESSFUL;
	}

	USHORT dsc_len = dsc->bDescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE ? 
			((USB_CONFIGURATION_DESCRIPTOR*)dsc)->wTotalLength : dsc->bLength;

	if (dsc_len > sizeof(*dsc) && dsc_len == r->TransferBufferLength) { // full descriptor
		cache_descriptor(vpdo, r, dsc);
	} else {
		TraceInfo(TRACE_WRITE, "%s: skip caching of descriptor: TransferBufferLength(%lu), dsc_len(%d)", 
					urb_function_str(r->Hdr.Function), r->TransferBufferLength, dsc_len);
	}

	return STATUS_SUCCESS;
}

/*
 * Buffer from the server has no gaps (compacted), SUM(src->actual_length) == src_len, 
 * src->offset is ignored for that reason.
 *
 * For isochronous packets: actual length is the sum of
 * the actual length of the individual, packets, but as
 * the packet offsets are not changed there will be
 * padding between the packets. To optimally use the
 * bandwidth the padding is not transmitted.
 *
 * See: 
 * <linux>/drivers/usb/usbip/stub_tx.c, stub_send_ret_submit
 * <linux>/drivers/usb/usbip/usbip_common.c, usbip_pad_iso
  */
static PAGEABLE NTSTATUS copy_isoc_data(
	struct _URB_ISOCH_TRANSFER *r, char *dst_buf, 
	const struct usbip_iso_packet_descriptor *src, const char *src_buf, ULONG src_len)
{
	PAGED_CODE();

	bool dir_out = !src_buf;
	USBD_ISO_PACKET_DESCRIPTOR *dst = r->IsoPacket;
	
	ULONG src_offset = r->NumberOfPackets ? src->offset : 0;
	NT_ASSERT(!src_offset);

	for (ULONG i = 0; i < r->NumberOfPackets; ++i, ++src, ++dst) {
	
		dst->Status = src->status ? to_windows_status(src->status) : USBD_STATUS_SUCCESS;
		
		if (dir_out) {
			continue; // dst->Length not used for OUT transfers
		}

		if (!src->actual_length) {
			dst->Length = 0;
			continue;
		}

		if (src->actual_length > src->length) {
			TraceError(TRACE_WRITE, "src->actual_length(%u) > src->length(%u)", src->actual_length, src->length);
			return STATUS_INVALID_PARAMETER;
		}

		if (src->offset != dst->Offset) { // buffer is compacted, but offsets are intact
			TraceError(TRACE_WRITE, "src->offset(%u) != dst->Offset(%lu)", src->offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset > dst->Offset) {// source buffer has no gaps
			TraceError(TRACE_WRITE, "src_offset(%lu) > dst->Offset(%lu)", src_offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset + src->actual_length > src_len) {
			TraceError(TRACE_WRITE, "src_offset(%lu) + src->actual_length(%u) > src_len(%lu)", 
						src_offset, src->actual_length, src_len);

			return STATUS_INVALID_PARAMETER;
		}

		if (dst->Offset + src->actual_length > r->TransferBufferLength) {
			TraceError(TRACE_WRITE, "dst->Offset(%lu) + src->actual_length(%u) > r->TransferBufferLength(%lu)", 
				dst->Offset, src->actual_length, r->TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}

		RtlCopyMemory(dst_buf + dst->Offset, src_buf + src_offset, src->actual_length);
		
		dst->Length = src->actual_length;
		src_offset += src->actual_length;
	}

	bool ok = src_offset == src_len;
	if (!ok) {
		TraceError(TRACE_WRITE, "src_offset(%lu) != src_len(%lu)", src_offset, src_len);
	}

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * Layout: usbip_header, data(IN only), usbip_iso_packet_descriptor[].
 */
static PAGEABLE NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vpdo);

	const struct usbip_header_ret_submit *res = &hdr->u.ret_submit;
	int cnt = res->number_of_packets;

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	r->ErrorCount = res->error_count;

	if (cnt && cnt == res->error_count) {
		r->Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r->StartFrame = res->start_frame;
	}

	if (!(cnt >= 0 && (ULONG)cnt == r->NumberOfPackets)) {
		TraceError(TRACE_WRITE, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r->NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}

	if (!(res->actual_length >= 0 && (ULONG)res->actual_length <= r->TransferBufferLength)) {
		TraceError(TRACE_WRITE, "actual_length(%d) > TransferBufferLength(%lu)", res->actual_length, r->TransferBufferLength);
		return STATUS_INVALID_PARAMETER;
	}

	bool dir_in = is_transfer_direction_in(hdr); // TransferFlags can have wrong direction

	const char *src_buf = (char*)(hdr + 1);
	ULONG src_len = dir_in ? res->actual_length : 0;

	auto src = reinterpret_cast<const usbip_iso_packet_descriptor*>(src_buf + src_len);

	void *ptr = r->Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? nullptr : r->TransferBuffer;
	auto buf = (char*)get_urb_buffer(ptr, r->TransferBufferMDL);
	
	return buf ? copy_isoc_data(r, buf, src, dir_in ? src_buf : nullptr, src_len) : 
		     get_copy_status(buf);
}

/*
* Nothing to handle.
*/
static PAGEABLE NTSTATUS urb_function_success(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(urb);
	UNREFERENCED_PARAMETER(hdr);
	
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_function_unexpected(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(hdr);

	USHORT func = urb->UrbHeader.Function;
	TraceError(TRACE_WRITE, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	return STATUS_INTERNAL_ERROR;
}	

typedef NTSTATUS (*urb_function_t)(vpdo_dev_t*, URB*, const struct usbip_header*);

static const urb_function_t urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_function_unexpected, // URB_FUNCTION_ABORT_PIPE, urb_pipe_request

	urb_function_unexpected, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	urb_function_unexpected, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	urb_function_unexpected, // URB_FUNCTION_GET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_GET_CURRENT_FRAME_NUMBER

	urb_function_generic, // URB_FUNCTION_CONTROL_TRANSFER
	urb_function_generic, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER
	urb_isoch_transfer,

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE 

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_DEVICE
	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_INTERFACE
	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVED_0X0016          

	urb_function_generic, // URB_FUNCTION_VENDOR_DEVICE
	urb_function_generic, // URB_FUNCTION_VENDOR_INTERFACE
	urb_function_generic, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_function_generic, // URB_FUNCTION_CLASS_DEVICE 
	urb_function_generic, // URB_FUNCTION_CLASS_INTERFACE
	urb_function_generic, // URB_FUNCTION_CLASS_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVE_0X001D

	urb_function_success, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL, urb_pipe_request

	urb_function_generic, // URB_FUNCTION_CLASS_OTHER
	urb_function_generic, // URB_FUNCTION_VENDOR_OTHER

	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_OTHER, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER, urb_control_feature_request

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	urb_function_generic, // URB_FUNCTION_GET_CONFIGURATION
	urb_function_generic, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

	urb_function_unexpected, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	nullptr, // URB_FUNCTION_RESERVE_0X002B
	nullptr, // URB_FUNCTION_RESERVE_0X002C
	nullptr, // URB_FUNCTION_RESERVE_0X002D
	nullptr, // URB_FUNCTION_RESERVE_0X002E
	nullptr, // URB_FUNCTION_RESERVE_0X002F

	urb_function_unexpected, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	urb_function_unexpected, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_function_generic, // URB_FUNCTION_CONTROL_TRANSFER_EX

	nullptr, // URB_FUNCTION_RESERVE_0X0033
	nullptr, // URB_FUNCTION_RESERVE_0X0034                  

	urb_function_unexpected, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_function_unexpected, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	urb_function_generic, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	urb_isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	nullptr, // 0x0039
	nullptr, // 0x003A        
	nullptr, // 0x003B        
	nullptr, // 0x003C        

	urb_function_unexpected // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

/*
 * URB function must:
 * 1.Take into account UrbHeader.Status which was mapped from ret_submit.status.
 *   For example, select_configuration/select_interface immediately returns in such case.
 * 2.If response from a server has data (actual_length > 0), the function MUST copy it to URB 
 *   even if UrbHeader.Status != USBD_STATUS_SUCCESS. Data was sent over the network, do not ditch it.
 */
static PAGEABLE NTSTATUS usb_submit_urb(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	PAGED_CODE(); 

	if (urb) {
		int err = hdr->u.ret_submit.status;
		urb->UrbHeader.Status = err ? to_windows_status(err) : USBD_STATUS_SUCCESS;
	} else {
		TraceError(TRACE_WRITE, "URB is null");
		return STATUS_INVALID_PARAMETER;
	}

	USHORT func = urb->UrbHeader.Function;
	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;

	NTSTATUS err = pfunc ? pfunc(vpdo, urb, hdr) : STATUS_INVALID_PARAMETER;

	if (err && !urb->UrbHeader.Status) {
		urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
		TraceInfo(TRACE_WRITE, "Set USBD_STATUS=%s because return is %!STATUS!", dbg_usbd_status(urb->UrbHeader.Status), err);
	} else if (urb->UrbHeader.Status && !err) {
		err = STATUS_UNSUCCESSFUL;
		TraceInfo(TRACE_WRITE, "Return %!STATUS! because USBD_STATUS=%s(%#08lX)", 
					err, dbg_usbd_status(urb->UrbHeader.Status), (ULONG)urb->UrbHeader.Status);
	}

	return err;
}

static PAGEABLE NTSTATUS get_descriptor_from_node_connection(IRP *irp, const struct usbip_header *hdr)
{
	PAGED_CODE();

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // r->Data[]

	int actual_length = hdr->u.ret_submit.actual_length;

	if (!(actual_length >= 0 && (ULONG)actual_length <= data_sz)) {
		TraceError(TRACE_WRITE, "OutputBufferLength %lu, actual_length(%d)", data_sz, actual_length);
		return STATUS_INVALID_PARAMETER;
	}

	auto r = (USB_DESCRIPTOR_REQUEST*)get_irp_buffer(irp);
	RtlCopyMemory(r->Data, hdr + 1, actual_length);
	
	TraceVerbose(TRACE_WRITE, "ConnectionIndex %lu, Data[%!BIN!]", r->ConnectionIndex, WppBinary(r->Data, (USHORT)actual_length));

	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS usb_reset_port(const struct usbip_header *hdr)
{
	PAGED_CODE();
	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS ret_submit(struct urb_req *urbr, const struct usbip_header *hdr)
{
	PAGED_CODE();

	IRP *irp = urbr->irp;
	if (!irp) {
		return STATUS_SUCCESS;
	}

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = usb_submit_urb(urbr->vpdo, (URB*)URB_FROM_IRP(irp), hdr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = usb_reset_port(hdr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(irp, hdr);
		break;
	default:
		TraceError(TRACE_WRITE, "Unexpected IoControlCode %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return st;
}

static PAGEABLE NTSTATUS ret_unlink(const struct usbip_header *hdr)
{
	PAGED_CODE();
	return hdr->u.ret_unlink.status ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static PAGEABLE const struct usbip_header *consume_write_irp_buffer(IRP *irp)
{
	PAGED_CODE();

	const auto hdr = (usbip_header*)get_irp_buffer(irp);
	ULONG buf_sz = get_irp_buffer_size(irp);

	if (buf_sz < sizeof(*hdr)) {
		TraceError(TRACE_WRITE, "usbip header expected: write buffer %lu", buf_sz);
		return nullptr;
	}

	size_t pdu_sz = get_pdu_size(hdr); 

	{
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceInfo(TRACE_WRITE, "IN %Iu%s", pdu_sz, dbg_usbip_hdr(buf, sizeof(buf), hdr));
	}

	if (buf_sz == pdu_sz) {
		TRANSFERRED(irp) = buf_sz;
		return hdr;
	}
	
	TraceError(TRACE_WRITE, "Write buffer %lu != pdu size %Iu", buf_sz, pdu_sz);
	return nullptr;
}

/*
 * Code must be in nonpaged section if it acquires spinlock.
 */
static void complete_irp(IRP *irp, NTSTATUS status)
{
	KIRQL oldirql;

	IoAcquireCancelSpinLock(&oldirql);
	bool valid_irp = IoSetCancelRoutine(irp, nullptr);
	IoReleaseCancelSpinLock(oldirql);

	if (!valid_irp) {
		return;
	}

	irp->IoStatus.Status = status;

	/* it seems windows client usb driver will think
	* IoCompleteRequest is running at DISPATCH_LEVEL
	* so without this it will change IRQL sometimes,
	* and introduce to a dead of my userspace program
	*/
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	KeLowerIrql(oldirql);
}

static PAGEABLE NTSTATUS do_write_irp(vpdo_dev_t *vpdo, IRP *write_irp)
{
	PAGED_CODE();

	const struct usbip_header *hdr = consume_write_irp_buffer(write_irp);
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	struct urb_req *urbr = find_sent_urbr(vpdo, hdr->base.seqnum);
	if (!urbr) {
		TraceInfo(TRACE_WRITE, "urb_req not found (cancelled?), seqnum %u", hdr->base.seqnum);
		return STATUS_SUCCESS;
	}
	
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	switch (hdr->base.command) {
	case USBIP_RET_SUBMIT:
		status = ret_submit(urbr, hdr);
		break;
	case USBIP_RET_UNLINK:
		status = ret_unlink(hdr);
		break;
	default:
		TraceError(TRACE_WRITE, "USBIP_RET_* expected, got %!usbip_request_type!", hdr->base.command);
	}

	IRP *irp = urbr->irp;
	free_urbr(urbr);
	if (irp) {
		complete_irp(irp, status);
	}

	return STATUS_SUCCESS;
}

/*
* WriteFile -> IRP_MJ_WRITE -> vhci_write
*/
extern "C" PAGEABLE NTSTATUS vhci_write(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(!TRANSFERRED(irp));

	TraceVerbose(TRACE_WRITE, "Enter irql %!irql!, write buffer %lu", KeGetCurrentIrql(), get_irp_buffer_size(irp));

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		TraceWarning(TRACE_WRITE, "write for non-vhci is not allowed");
		return irp_done(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->common.DevicePnPState != Deleted) {

		IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
		auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext);
		
		if (vpdo && vpdo->plugged) {
			status = do_write_irp(vpdo, irp);
		} else {
			TraceVerbose(TRACE_WRITE, "null or unplugged");
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	NT_ASSERT(TRANSFERRED(irp) <= get_irp_buffer_size(irp));
	TraceVerbose(TRACE_WRITE, "Leave %!STATUS!, transferred %Iu", status, TRANSFERRED(irp));

	return irp_done(irp, status);
}