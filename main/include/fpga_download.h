#pragma once

#include "ice40.h"
#include "pax_gfx.h"
#include "ili9341.h"

void fpga_download(ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341);
