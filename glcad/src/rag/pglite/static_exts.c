#include <stddef.h>
#include <string.h>

#define PGL_STATIC_EXT_MAX 8

typedef struct PglStaticExt {
	const char *name;
	const char *const *symbol_names;
	void *const *symbol_addrs;
	int nsyms;
} PglStaticExt;

static PglStaticExt pgl_static_exts[PGL_STATIC_EXT_MAX];
static int pgl_static_ext_count = 0;

void
pgl_register_static_ext(const char *name, const char *const *symbol_names,
						void *const *symbol_addrs, int nsyms)
{
	if (pgl_static_ext_count >= PGL_STATIC_EXT_MAX)
		return;
	pgl_static_exts[pgl_static_ext_count].name = name;
	pgl_static_exts[pgl_static_ext_count].symbol_names = symbol_names;
	pgl_static_exts[pgl_static_ext_count].symbol_addrs = symbol_addrs;
	pgl_static_exts[pgl_static_ext_count].nsyms = nsyms;
	pgl_static_ext_count++;
}

void *
pgl_ext_dlopen(const char *file)
{
	const char *base = file;
	const char *p;
	int			i;

	for (p = file; *p; p++)
		if (*p == '/')
			base = p + 1;
	for (i = 0; i < pgl_static_ext_count; i++)
	{
		size_t		n = strlen(pgl_static_exts[i].name);

		if (strncmp(base, pgl_static_exts[i].name, n) == 0 &&
			(base[n] == '\0' || base[n] == '.'))
			return (void *) &pgl_static_exts[i];
	}
	return NULL;
}

static int
pgl_ext_handle_index(void *handle)
{
	PglStaticExt *ext = (PglStaticExt *) handle;

	if (ext >= pgl_static_exts && ext < pgl_static_exts + pgl_static_ext_count)
		return (int) (ext - pgl_static_exts);
	return -1;
}

void *
pgl_ext_dlsym(void *handle, const char *symbol)
{
	int			idx = pgl_ext_handle_index(handle);
	int			i;

	if (idx < 0)
		return NULL;
	for (i = 0; i < pgl_static_exts[idx].nsyms; i++)
		if (strcmp(pgl_static_exts[idx].symbol_names[i], symbol) == 0)
			return pgl_static_exts[idx].symbol_addrs[i];
	return NULL;
}

int
pgl_ext_dlclose(void *handle)
{
	return pgl_ext_handle_index(handle) >= 0;
}

extern void Pg_magic_func(void);
extern void _PG_init(void);
extern void plpgsql_call_handler(void);
extern void plpgsql_inline_handler(void);
extern void plpgsql_validator(void);
extern void pg_finfo_plpgsql_call_handler(void);
extern void pg_finfo_plpgsql_inline_handler(void);
extern void pg_finfo_plpgsql_validator(void);

static const char *const plpgsql_symbol_names[] = {
	"Pg_magic_func",
	"_PG_init",
	"plpgsql_call_handler",
	"plpgsql_inline_handler",
	"plpgsql_validator",
	"pg_finfo_plpgsql_call_handler",
	"pg_finfo_plpgsql_inline_handler",
	"pg_finfo_plpgsql_validator",
};

static void *const plpgsql_symbol_addrs[] = {
	(void *) Pg_magic_func,
	(void *) _PG_init,
	(void *) plpgsql_call_handler,
	(void *) plpgsql_inline_handler,
	(void *) plpgsql_validator,
	(void *) pg_finfo_plpgsql_call_handler,
	(void *) pg_finfo_plpgsql_inline_handler,
	(void *) pg_finfo_plpgsql_validator,
};

extern void glcad_pgl_register_vector(void);

void
glcad_pgl_register_static_exts(void)
{
	static int	done = 0;

	if (done)
		return;
	done = 1;
	pgl_register_static_ext("plpgsql", plpgsql_symbol_names, plpgsql_symbol_addrs,
							(int) (sizeof(plpgsql_symbol_names) / sizeof(plpgsql_symbol_names[0])));
	glcad_pgl_register_vector();
}
