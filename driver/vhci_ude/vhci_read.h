#pragma once

#include <ntdef.h>
#include <wdftypes.h>

VOID
io_read(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len);