/* Copyright (c) 2016-2017 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "dovecot-openssl-common.h"

#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <openssl/rand.h>

static int openssl_init_refcount = 0;
static ENGINE *dovecot_openssl_engine;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
static void *dovecot_openssl_malloc(size_t size, const char *u0 ATTR_UNUSED, int u1 ATTR_UNUSED)
#else
static void *dovecot_openssl_malloc(size_t size)
#endif
{
	/* this may be performance critical, so don't use
	   i_malloc() or calloc() */
	void *mem = malloc(size);
	if (mem == NULL) {
		i_fatal_status(FATAL_OUTOFMEM,
			"OpenSSL: malloc(%"PRIuSIZE_T"): Out of memory", size);
	}
	return mem;
}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
static void *dovecot_openssl_realloc(void *ptr, size_t size, const char *u0 ATTR_UNUSED, int u1 ATTR_UNUSED)
#else
static void *dovecot_openssl_realloc(void *ptr, size_t size)
#endif
{
	void *mem = realloc(ptr, size);
	if (mem == NULL) {
		i_fatal_status(FATAL_OUTOFMEM,
			"OpenSSL: realloc(%"PRIuSIZE_T"): Out of memory", size);
	}
	return mem;
}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
static void dovecot_openssl_free(void *ptr, const char *u0 ATTR_UNUSED, int u1 ATTR_UNUSED)
#else
static void dovecot_openssl_free(void *ptr)
#endif
{
	free(ptr);
}

void dovecot_openssl_common_global_ref(void)
{
	unsigned char buf;

	if (openssl_init_refcount++ > 0)
		return;

	/* use our own memory allocation functions that will die instead of
	   returning NULL. this avoids random failures on out-of-memory
	   conditions. */
	if (CRYPTO_set_mem_functions(dovecot_openssl_malloc,
				     dovecot_openssl_realloc, dovecot_openssl_free) == 0) {
		/*i_warning("CRYPTO_set_mem_functions() was called too late");*/
	}

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	/* PRNG initialization might want to use /dev/urandom, make sure it
	   does it before chrooting. We might not have enough entropy at
	   the first try, so this function may fail. It's still been
	   initialized though. */
	(void)RAND_bytes(&buf, 1);
}

bool dovecot_openssl_common_global_unref(void)
{
	i_assert(openssl_init_refcount > 0);

	if (--openssl_init_refcount > 0)
		return TRUE;

	if (dovecot_openssl_engine != NULL) {
		ENGINE_finish(dovecot_openssl_engine);
		dovecot_openssl_engine = NULL;
	}
#if OPENSSL_VERSION_NUMBER < 0x10001000L
	OBJ_cleanup();
#endif
#ifdef HAVE_SSL_COMP_FREE_COMPRESSION_METHODS
	SSL_COMP_free_compression_methods();
#endif
	ENGINE_cleanup();
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
#if OPENSSL_VERSION_NUMBER < 0x10000000L
	ERR_remove_state(0);
#elif OPENSSL_VERSION_NUMBER < 0x10100000L
	ERR_remove_thread_state(NULL);
#endif
	ERR_free_strings();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined (LIBRESSL_VERSION_NUMBER)
	OPENSSL_cleanup();
#endif
	return FALSE;
}

int dovecot_openssl_common_global_set_engine(const char *engine,
					     const char **error_r)
{
	if (dovecot_openssl_engine != NULL)
		return 1;

	ENGINE_load_builtin_engines();
	dovecot_openssl_engine = ENGINE_by_id(engine);
	if (dovecot_openssl_engine == NULL) {
		*error_r = t_strdup_printf("Unknown engine '%s'", engine);
		return 0;
	}
	if (ENGINE_init(dovecot_openssl_engine) == 0) {
		*error_r = t_strdup_printf("ENGINE_init(%s) failed", engine);
		ENGINE_free(dovecot_openssl_engine);
		dovecot_openssl_engine = NULL;
		return -1;
	}
	if (ENGINE_set_default(dovecot_openssl_engine, ENGINE_METHOD_ALL) == 0) {
		*error_r = t_strdup_printf("ENGINE_set_default(%s) failed", engine);
		ENGINE_free(dovecot_openssl_engine);
		dovecot_openssl_engine = NULL;
		return -1;
	}
	return 1;
}
