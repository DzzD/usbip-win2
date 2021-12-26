#include "strutil.h"

#include <ntddk.h>
#include <ntstrsafe.h>

const ULONG libdrv_pooltag = 'dbil';

LPWSTR libdrv_strdupW(LPCWSTR str)
{
	size_t len = 0;
	NTSTATUS st = RtlStringCchLengthW(str, NTSTRSAFE_MAX_CCH, &len);
	if (st != STATUS_SUCCESS) {
		return nullptr;
	}

	size_t sz = ++len*sizeof(*str);

	auto s = (LPWSTR)ExAllocatePoolWithTag(PagedPool, sz, libdrv_pooltag);
	if (s) {
		RtlCopyMemory(s, str, sz);
	}

	return s;
}

void libdrv_free(void *data)
{
	if (data) {
		ExFreePoolWithTag(data, libdrv_pooltag);
	}
}