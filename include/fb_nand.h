/*
 * Copyright 2014 Broadcom Corporation.
 * Copyright 2015 Free Electrons.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

void fb_nand_flash_read(const char *partname, unsigned int session_id,
			 void *upload_buffer, unsigned int upload_bytes,
			 char *response);
void fb_nand_flash_write(const char *cmd, unsigned int session_id,
			 void *download_buffer, unsigned int download_bytes,
			 char *response);
void fb_nand_erase(const char *cmd, char *response);
