#include <common.h>
#include <malloc.h>
#include <libfdt.h>

#define fdt_for_each_property(fdt, node, property)		\
	for (property = fdt_first_property_offset(fdt, node);	\
	     property >= 0;					\
	     property = fdt_next_property_offset(fdt, property))

struct fdt_overlay_fixup {
	char	label[64];
	char	name[64];
	char	path[64];
	int	index;
};

static int fdt_get_max_phandle(const void *dt)
{
	int offset, phandle = 0, new_phandle;
	
	for (offset = fdt_next_node(dt, -1, NULL); offset >= 0;
	     offset = fdt_next_node(dt, offset, NULL)) {
		new_phandle = fdt_get_phandle(dt, offset);
		if (new_phandle > phandle)
			phandle = new_phandle;
	}

	return phandle;
}

static uint32_t fdt_overlay_get_target_phandle(const void *dt, int node)
{
	const uint32_t *val;
	int len;

	val = fdt_getprop(dt, node, "target", &len);
	if (!val || (len != sizeof(*val)))
		return 0;

	return fdt32_to_cpu(*val);
}

static int fdt_overlay_get_target(const void *dt, const void *dto, int fragment)
{
	uint32_t phandle;
	const char *path;

	/* Try first to do a phandle based lookup */
	phandle = fdt_overlay_get_target_phandle(dto, fragment);
	if (phandle)
		return fdt_node_offset_by_phandle(dt, phandle);

	/* And then a path based lookup */
	path = fdt_getprop(dto, fragment, "target-path", NULL);
	if (!path)
		return -FDT_ERR_NOTFOUND;

	return fdt_path_offset(dt, path);
}

static int fdt_overlay_adjust_node_phandles(void *dto, int node,
					    uint32_t delta)
{
	int property;
	int child;

	fdt_for_each_property(dto, node, property) {
		const uint32_t *val;
		const char *name;
		uint32_t adj_val;
		int len;
		int ret;

		val = fdt_getprop_by_offset(dto, property,
					    &name, &len);
		if (!val || (len != 4))
			continue;

		if (strcmp(name, "phandle") && strcmp(name, "linux,phandle"))
			continue;

		adj_val = fdt32_to_cpu(*val);
		adj_val += delta;
		ret = fdt_setprop_inplace_u32(dto, node, name, adj_val);
		if (ret) {
			printf("Failed to ajust property %s phandle\n", name);
			return ret;
		}
	}

	fdt_for_each_subnode(dto, child, node)
		fdt_overlay_adjust_node_phandles(dto, child, delta);

	return 0;
}

static int fdt_overlay_adjust_local_phandles(void *overlay, uint32_t delta)
{
	int root;

	root = fdt_path_offset(overlay, "/");
	if (root < 0) {
		printf("Couldn't locate the root of the overlay\n");
		return root;
	}

	return fdt_overlay_adjust_node_phandles(overlay, root, delta);
}

static int _fdt_overlay_update_local_references(void *dto,
						int tree_node,
						int fixup_node,
						uint32_t delta)
{
	int fixup_prop;
	int fixup_child;

	fdt_for_each_property(dto, fixup_node, fixup_prop) {
		const char *fixup_name, *tree_name;
		const uint32_t *val;
		uint32_t adj_val;
		int fixup_len;
		int tree_prop;
		int ret;

		fdt_getprop_by_offset(dto, fixup_prop,
				      &fixup_name, &fixup_len);

		if (!strcmp(fixup_name, "phandle") ||
		    !strcmp(fixup_name, "linux,phandle"))
			continue;

		if (fixup_len != 4) {
			printf("Illegal property size (%d) %s\n",
			       fixup_len, fixup_name);
			return -FDT_ERR_NOTFOUND;
		}

		fdt_for_each_property(dto, tree_node, tree_prop) {
			int tree_len;

			val = fdt_getprop_by_offset(dto, tree_prop,
						    &tree_name, &tree_len);

			if (!strcmp(tree_name, fixup_name))
				break;
		}

		if (!tree_name) {
			printf("Couldn't find target property %s\n", fixup_name);
			return -FDT_ERR_NOTFOUND;
		}

		adj_val = fdt32_to_cpu(*val);
		adj_val += delta;
		ret = fdt_setprop_inplace_u32(dto, tree_node, fixup_name,
					      adj_val);
		if (ret) {
			printf("Failed to ajust property %s phandle\n",
			       fixup_name);
			return ret;
		}
	}

	fdt_for_each_subnode(dto, fixup_child, fixup_node) {
		const char *fixup_child_name = fdt_get_name(dto,
							    fixup_child, NULL);
		int tree_child;

		fdt_for_each_subnode(dto, tree_child, tree_node) {
			const char *tree_child_name = fdt_get_name(dto,
								   tree_child,
								   NULL);

			if (!strcmp(fixup_child_name, tree_child_name))
				break;
		}

		_fdt_overlay_update_local_references(dto,
						     tree_child,
						     fixup_child,
						     delta);
	}

	return 0;
}

static int fdt_overlay_update_local_references(void *dto, uint32_t delta)
{
	int root, fixups;

	root = fdt_path_offset(dto, "/");
	if (root < 0) {
		printf("Couldn't locate the root of the overlay\n");
		return root;
	}

	fixups = fdt_path_offset(dto, "/__local_fixups__");
	if (root < 0) {
		printf("Couldn't locate the local fixups in the overlay\n");
		return root;
	}

	return _fdt_overlay_update_local_references(dto, root, fixups,
						    delta);
}

static struct fdt_overlay_fixup *fdt_fixups_parse_property(void *dto,
							   int property,
							   int *number)
{
	struct fdt_overlay_fixup *fixups;
	const char *value, *tmp_value;
	const char *name;
	int tmp_len, len, next;
	int table = 0;
	int i;

	value = fdt_getprop_by_offset(dto, property,
				      &name, &len);
	tmp_value = value;
	tmp_len = len;

	do {
		next = strlen(tmp_value) + 1;
		tmp_len -= next;
		tmp_value += next;
		table++;
	} while (tmp_len > 0);

	fixups = malloc(table * sizeof(*fixups));
	if (!fixups)
		return NULL;

	for (i = 0; i < table; i++) {
		struct fdt_overlay_fixup *fixup = fixups + i;
		const char *prop_string = value;
		char *sep;
		int stlen;

		stlen = strlen(prop_string);

		sep = strchr(prop_string, ':');
		strncpy(fixup->path, prop_string, sep - prop_string);
		fixup->path[sep - prop_string] = '\0';
		prop_string = sep + 1;

		sep = strchr(prop_string, ':');
		strncpy(fixup->name, prop_string, sep - prop_string);
		fixup->name[sep - prop_string] = '\0';
		prop_string = sep + 1;

		fixup->index = simple_strtol(prop_string, NULL, 10);
		strncpy(fixup->label, name, 64);

		value += stlen + 1;
		len -= stlen + 1;
	}

	*number = table;
	return fixups;
}

static int fdt_overlay_fixup_phandles(void *dt, void *dto)
{
	int fixups_off, symbols_off;
	int property;

	symbols_off = fdt_path_offset(dt, "/__symbols__");
	fixups_off = fdt_path_offset(dto, "/__fixups__");

	fdt_for_each_property(dto, fixups_off, property) {
		struct fdt_overlay_fixup *fixups;
		int n_fixups;
		int i;

		fixups = fdt_fixups_parse_property(dto, property, &n_fixups);
		if (!fixups || n_fixups == 0)
			continue;

		for (i = 0; i < n_fixups; i++) {
			struct fdt_overlay_fixup *fixup = fixups + i;
			const uint32_t *prop_val;
			const char *symbol_path;
			uint32_t *fixup_val;
			uint32_t phandle;
			int symbol_off, fixup_off;
			int prop_len;
			int ret;

			symbol_path = fdt_getprop(dt, symbols_off, fixup->label, &prop_len);
			if (!symbol_path) {
				printf("Couldn't lookup symbol %s in the main DT... Skipping\n",
				       fixup->label);
				continue;
			}

			symbol_off = fdt_path_offset(dt, symbol_path);
			if (symbol_off < 0) {
				printf("Couldn't match the symbol %s to node %s... Skipping\n",
				       fixup->label, symbol_path);
				continue;
			}

			phandle = fdt_get_phandle(dt, symbol_off);
			if (!phandle) {
				printf("Symbol node %s has no phandle... Skipping\n",
				       symbol_path);
				continue;
			}

			fixup_off = fdt_path_offset(dto, fixup->path);
			if (fixup_off < 0) {
				printf("Invalid overlay node %s to fixup... Skipping\n",
				       fixup->path);
				continue;
			}

			prop_val = fdt_getprop(dto, fixup_off, fixup->name,
					       &prop_len);
			if (!prop_val) {
				printf("Couldn't retrieve property %s/%s value... Skipping\n",
				       fixup->path, fixup->name);
				continue;
			}

			fixup_val = malloc(prop_len);
			if (!fixup_val)
				return -FDT_ERR_INTERNAL;
			memcpy(fixup_val, prop_val, prop_len);

			if (fdt32_to_cpu(fixup_val[fixup->index]) != 0xdeadbeef) {
				printf("Value pointed (0x%x) is not supposed to be fixed up... Skipping\n",
				       fdt32_to_cpu(fixup_val[fixup->index]));
				continue;
			}

			fixup_val[fixup->index] = cpu_to_fdt32(phandle);
			ret = fdt_setprop_inplace(dto, fixup_off,
						  fixup->name, fixup_val,
						  prop_len);
			if (ret) {
				printf("Couldn't fixup phandle in overlay property %s/%s (%d)... Skipping\n",
				       fixup->path, fixup->name, ret);
			}
		}

		free(fixups);
	}

	return 0;
}

static int fdt_apply_overlay_node(void *dt, void *dto,
				  int target, int overlay)
{
	int property;
	int node;

	fdt_for_each_property(dto, overlay, property) {
		const char *name;
		const void *prop;
		int prop_len;
		int ret;

		prop = fdt_getprop_by_offset(dto, property, &name,
					     &prop_len);
		if (!prop)
			return -FDT_ERR_INTERNAL;

		ret = fdt_setprop(dt, target, name, prop, prop_len);
		if (ret) {
			printf("Couldn't set property %s\n", name);
			return ret;
		}
	}

	fdt_for_each_subnode(dto, node, overlay) {
		const char *name = fdt_get_name(dto, node, NULL);
		int nnode;
		int ret;

		nnode = fdt_add_subnode(dt, target, name);
		if (nnode < 0) {
			printf("Couldn't add subnode %s (%d)\n", name, nnode);
			return nnode;
		}

		ret = fdt_apply_overlay_node(dt, dto, nnode, node);
		if (ret) {
			printf("Couldn't apply sub-overlay (%d)\n", ret);
			return ret;
		}
	}

	return 0;
}

static int fdt_overlay_merge(void *dt, void *dto)
{
	int root, fragment;

	root = fdt_path_offset(dto, "/");
	if (root < 0) {
		printf("Couldn't locate the root of our overlay\n");
		return root;
	}

	fdt_for_each_subnode(dto, fragment, root) {
		const char *name = fdt_get_name(dto, fragment, NULL);
		uint32_t target;
		int overlay;
		int ret;

		if (strncmp(name, "fragment", 8))
			continue;

		target = fdt_overlay_get_target(dt, dto, fragment);
		if (target < 0) {
			printf("Couldn't locate %s target\n", name);
			return target;
		}

		overlay = fdt_subnode_offset(dto, fragment, "__overlay__");
		if (overlay < 0) {
			printf("Couldn't locate %s overlay\n", name);
			return overlay;
		}

		ret = fdt_apply_overlay_node(dt, dto, target, overlay);
		if (ret) {
			printf("Couldn't apply %s overlay\n", name);
			return ret;
		}
	}

	return 0;
}

int fdt_overlay_apply(void *dt, void *dto)
{
	uint32_t delta = fdt_get_max_phandle(dt) + 1;
	int ret;

	ret = fdt_overlay_adjust_local_phandles(dto, delta);
	if (ret) {
		printf("Couldn't adjust local phandles\n");
		return ret;
	}

	ret = fdt_overlay_update_local_references(dto, delta);
	if (ret) {
		printf("Couldn't update our local references\n");
		return ret;
	}

	ret = fdt_overlay_fixup_phandles(dt, dto);
	if (ret) {
		printf("Couldn't resolve the global phandles\n");
		return ret;
	}

	return fdt_overlay_merge(dt, dto);
}
