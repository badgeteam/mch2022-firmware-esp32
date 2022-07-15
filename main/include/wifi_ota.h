#pragma once

#include "ili9341.h"
#include "pax_gfx.h"

void ota_update(pax_buf_t* pax_buffer, ILI9341* ili9341, bool nightly);
