// Resource ID for the embedded NeacSafe64 driver (Driver.sys) shipped
// inside the exe. Loaded at runtime via FindResource / LoadResource
// when no on-disk Driver.sys is found at the standard lookup paths.
#pragma once
#define IDR_NEACSAFE_DRIVER  101
