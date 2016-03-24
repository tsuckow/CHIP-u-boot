/*
 * Copyright 2014 Broadcom Corporation.
 * Copyright 2015 Free Electrons.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>

#include <fastboot.h>
#include <image-sparse.h>
#include <sparse_format.h>

#include <linux/mtd/mtd.h>
#include <jffs2/jffs2.h>
#include <nand.h>

static char *response_str;

struct fb_nand_sparse {
	nand_info_t		*nand;
	struct part_info	*part;
};

__weak int board_fastboot_erase_partition_setup(char *name)
{
	return 0;
}

__weak int board_fastboot_write_partition_setup(char *name)
{
	return 0;
}

static int fb_nand_lookup(const char *partname, char *response,
			  nand_info_t **nand,
			  struct part_info **part)
{
	struct mtd_device *dev;
	int ret;
	u8 pnum;

	ret = mtdparts_init();
	if (ret) {
		error("Cannot initialize MTD partitions\n");
		fastboot_fail(response_str, "cannot init mtdparts");
		return ret;
	}

	ret = find_dev_and_part(partname, &dev, &pnum, part);
	if (ret) {
		error("cannot find partition: '%s'", partname);
		fastboot_fail(response_str, "cannot find partition");
		return ret;
	}

	if (dev->id->type != MTD_DEV_TYPE_NAND) {
		error("partition '%s' is not stored on a NAND device",
		      partname);
		fastboot_fail(response_str, "not a NAND device");
		return -EINVAL;
	}

	*nand = &nand_info[dev->id->num];

	return 0;
}

static int _fb_nand_erase(nand_info_t *nand, struct part_info *part)
{
	nand_erase_options_t opts;
	int ret;

	memset(&opts, 0, sizeof(opts));
	opts.offset = part->offset;
	opts.length = part->size;
	opts.quiet = 1;

	printf("Erasing blocks 0x%llx to 0x%llx\n",
	       part->offset, part->offset + part->size);

	ret = nand_erase_opts(nand, &opts);
	if (ret)
		return ret;

	printf("........ erased 0x%llx bytes from '%s'\n",
	       part->size, part->name);

	return 0;
}

static int _fb_nand_raw_write(nand_info_t *nand, unsigned int offset,
			      void *buffer, unsigned int length)
{
	unsigned int page_count;
	int ret, i;

	page_count = length / nand->writesize;

	for (i = 0; i < page_count; i++) {
		mtd_oob_ops_t ops = {
			.datbuf = (u8 *)buffer,
			.oobbuf = ((u8 *)buffer) + nand->writesize,
			.len = nand->writesize,
			.ooblen = nand->oobsize,
			.mode = MTD_OPS_RAW
		};

		ret = mtd_write_oob(nand, offset, &ops);
		if (ret) {
			printf("%s: error at offset %llx, ret %d\n",
			       __func__, (long long)offset, ret);
			break;
		}

		buffer += nand->writesize + nand->oobsize;
		offset += nand->writesize;
	}

	return 0;
}

static int _fb_nand_write(nand_info_t *nand, struct part_info *part,
			  void *buffer, unsigned int offset,
			  unsigned int length, size_t *written)
{
	int flags = WITH_WR_VERIFY;

#ifdef CONFIG_FASTBOOT_FLASH_NAND_TRIMFFS
	flags |= WITH_DROP_FFS;
#endif

	if (!strcmp(part->name, "spl") || !strcmp(part->name, "spl-backup")) {
		*written = length;
		return _fb_nand_raw_write(nand, offset, buffer, length);
	}

	if (!strcmp(part->name, "UBI"))
		flags |= WITH_SLC_MODE;

	return nand_write_skip_bad(nand, offset, &length, written,
				   part->size - (offset - part->offset),
				   buffer, flags);
}

static int fb_nand_sparse_write(struct sparse_storage *storage,
				void *priv,
				unsigned int offset,
				unsigned int size,
				char *data,
				unsigned int *new_offset)
{
	struct fb_nand_sparse *sparse = priv;
	size_t written;
	int ret;

	printf("Writing at offset 0x%x\n", offset * storage->block_sz);

	ret = _fb_nand_write(sparse->nand, sparse->part, data,
			     offset * storage->block_sz,
			     size * storage->block_sz, &written);
	if (ret < 0) {
		printf("Failed to write sparse chunk\n");
		return ret;
	}

	printf("New offset 0x%x\n", offset * storage->block_sz + written);

	*new_offset = offset + (written / storage->block_sz);
	return size;
}

void fb_nand_flash_write(const char *partname, unsigned int session_id,
			 void *download_buffer, unsigned int download_bytes,
			 char *response)
{
	struct part_info *part;
	nand_info_t *nand = NULL;
	int ret;

	/* initialize the response buffer */
	response_str = response;

	ret = fb_nand_lookup(partname, response, &nand, &part);
	if (ret) {
		error("invalid NAND device");
		fastboot_fail(response_str, "invalid NAND device");
		return;
	}

	ret = board_fastboot_write_partition_setup(part->name);
	if (ret)
		return;

	if (is_sparse_image(download_buffer)) {
		struct fb_nand_sparse sparse_priv;
		sparse_storage_t sparse;

		sparse_priv.nand = nand;
		sparse_priv.part = part;

		sparse.block_sz = nand->writesize;
		sparse.start = lldiv(part->offset, sparse.block_sz);
		sparse.size = lldiv(part->size, sparse.block_sz);
		sparse.name = part->name;
		sparse.write = fb_nand_sparse_write;

		ret = store_sparse_image(&sparse, &sparse_priv, session_id,
					 download_buffer);
	} else {
		printf("Flashing raw image at offset 0x%llx\n",
		       part->offset);

		ret = _fb_nand_write(nand, part, download_buffer, part->offset,
				     download_bytes, NULL);

		printf("........ wrote %u bytes to '%s'\n",
		       download_bytes, part->name);
	}

	if (ret) {
		fastboot_fail(response_str, "error writing the image");
		return;
	}

	fastboot_okay(response_str, "");
}

void fb_nand_erase(const char *partname, char *response)
{
	struct part_info *part;
	nand_info_t *nand = NULL;
	int ret;

	/* initialize the response buffer */
	response_str = response;

	ret = fb_nand_lookup(partname, response, &nand, &part);
	if (ret) {
		error("invalid NAND device");
		fastboot_fail(response_str, "invalid NAND device");
		return;
	}

	ret = board_fastboot_erase_partition_setup(part->name);
	if (ret)
		return;

	ret = _fb_nand_erase(nand, part);
	if (ret) {
		error("failed erasing from device %s", nand->name);
		fastboot_fail(response_str, "failed erasing from device");
		return;
	}

	fastboot_okay(response_str, "");
}
