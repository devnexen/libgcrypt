/* mac.c  -  message authentication code dispatcher
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This file is part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser general Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "g10lib.h"
#include "mac-internal.h"


/* This is the list of the digest implementations included in
   libgcrypt.  */
static gcry_mac_spec_t *mac_list[] = {
#if USE_SHA1
  &_gcry_mac_type_spec_hmac_sha1,
#endif
#if USE_SHA256
  &_gcry_mac_type_spec_hmac_sha256,
  &_gcry_mac_type_spec_hmac_sha224,
#endif
#if USE_SHA512
  &_gcry_mac_type_spec_hmac_sha512,
  &_gcry_mac_type_spec_hmac_sha384,
#endif
#ifdef USE_GOST_R_3411_94
  &_gcry_mac_type_spec_hmac_gost3411_94,
#endif
#ifdef USE_GOST_R_3411_12
  &_gcry_mac_type_spec_hmac_stribog256,
  &_gcry_mac_type_spec_hmac_stribog512,
#endif
#if USE_WHIRLPOOL
  &_gcry_mac_type_spec_hmac_whirlpool,
#endif
#if USE_RMD160
  &_gcry_mac_type_spec_hmac_rmd160,
#endif
#if USE_TIGER
  &_gcry_mac_type_spec_hmac_tiger1,
#endif
#if USE_MD5
  &_gcry_mac_type_spec_hmac_md5,
#endif
#if USE_MD4
  &_gcry_mac_type_spec_hmac_md4,
#endif
  NULL,
};



/* Return the spec structure for the MAC algorithm ALGO.  For an
   unknown algorithm NULL is returned.  */
static gcry_mac_spec_t *
spec_from_algo (int algo)
{
  gcry_mac_spec_t *spec;
  int idx;

  for (idx = 0; (spec = mac_list[idx]); idx++)
    if (algo == spec->algo)
      return spec;
  return NULL;
}


/* Lookup a mac's spec by its name.  */
static gcry_mac_spec_t *
spec_from_name (const char *name)
{
  gcry_mac_spec_t *spec;
  int idx;

  for (idx = 0; (spec = mac_list[idx]); idx++)
    if (!stricmp (name, spec->name))
      return spec;

  return NULL;
}


/****************
 * Map a string to the mac algo
 */
int
gcry_mac_map_name (const char *string)
{
  gcry_mac_spec_t *spec;

  if (!string)
    return 0;

  /* Not found, search a matching mac name.  */
  spec = spec_from_name (string);
  if (spec)
    return spec->algo;

  return 0;
}


/****************
 * This function simply returns the name of the algorithm or some constant
 * string when there is no algo.  It will never return NULL.
 * Use the macro gcry_mac_test_algo() to check whether the algorithm
 * is valid.
 */
const char *
gcry_mac_algo_name (int algorithm)
{
  gcry_mac_spec_t *spec;

  spec = spec_from_algo (algorithm);
  return spec ? spec->name : "?";
}


static gcry_err_code_t
check_mac_algo (int algorithm)
{
  gcry_mac_spec_t *spec;

  spec = spec_from_algo (algorithm);
  if (spec && !spec->flags.disabled)
    return 0;

  return GPG_ERR_MAC_ALGO;
}


/****************
 * Open a message digest handle for use with algorithm ALGO.
 */
static gcry_err_code_t
mac_open (gcry_mac_hd_t * hd, int algo, int secure, gcry_ctx_t ctx)
{
  gcry_mac_spec_t *spec;
  gcry_err_code_t err;
  gcry_mac_hd_t h;

  spec = spec_from_algo (algo);
  if (!spec)
    return GPG_ERR_MAC_ALGO;
  else if (spec->flags.disabled)
    return GPG_ERR_MAC_ALGO;
  else if (!spec->ops)
    return GPG_ERR_MAC_ALGO;
  else if (!spec->ops->open || !spec->ops->write || !spec->ops->setkey ||
           !spec->ops->read || !spec->ops->verify || !spec->ops->reset)
    return GPG_ERR_MAC_ALGO;

  if (secure)
    h = gcry_calloc_secure (1, sizeof (*h));
  else
    h = gcry_calloc (1, sizeof (*h));

  if (!h)
    return gpg_err_code_from_syserror ();

  h->magic = secure ? CTX_MAGIC_SECURE : CTX_MAGIC_NORMAL;
  h->spec = spec;
  h->algo = algo;
  h->gcry_ctx = ctx;

  err = h->spec->ops->open (h);
  if (err)
    gcry_free (h);
  else
    *hd = h;

  return err;
}


static gcry_error_t
mac_reset (gcry_mac_hd_t hd)
{
  if (hd->spec->ops->reset)
    return hd->spec->ops->reset (hd);

  return 0;
}


static void
mac_close (gcry_mac_hd_t hd)
{
  if (hd->spec->ops->close)
    hd->spec->ops->close (hd);

  wipememory (hd, sizeof (*hd));

  gcry_free (hd);
}


static gcry_error_t
mac_setkey (gcry_mac_hd_t hd, const void *key, size_t keylen)
{
  if (!hd->spec->ops->setkey)
    return GPG_ERR_INV_ARG;
  if (keylen > 0 && !key)
    return GPG_ERR_INV_ARG;

  return hd->spec->ops->setkey (hd, key, keylen);
}


static gcry_error_t
mac_setiv (gcry_mac_hd_t hd, const void *iv, size_t ivlen)
{
  if (!hd->spec->ops->setiv)
    return GPG_ERR_INV_ARG;
  if (ivlen > 0 && !iv)
    return GPG_ERR_INV_ARG;

  return hd->spec->ops->setiv (hd, iv, ivlen);
}


static gcry_error_t
mac_write (gcry_mac_hd_t hd, const void *inbuf, size_t inlen)
{
  if (!hd->spec->ops->write)
    return GPG_ERR_INV_ARG;
  if (inlen > 0 && !inbuf)
    return GPG_ERR_INV_ARG;

  return hd->spec->ops->write (hd, inbuf, inlen);
}


static gcry_error_t
mac_read (gcry_mac_hd_t hd, void *outbuf, size_t * outlen)
{
  if (!outbuf || !outlen || *outlen == 0 || !hd->spec->ops->read)
    return GPG_ERR_INV_ARG;

  return hd->spec->ops->read (hd, outbuf, outlen);
}


static gcry_error_t
mac_verify (gcry_mac_hd_t hd, const void *buf, size_t buflen)
{
  if (!buf || buflen == 0 || !hd->spec->ops->verify)
    return GPG_ERR_INV_ARG;

  return hd->spec->ops->verify (hd, buf, buflen);
}


/* Create a MAC object for algorithm ALGO.  FLAGS may be
   given as an bitwise OR of the gcry_mac_flags values.
   H is guaranteed to be a valid handle or NULL on error.  */
gcry_error_t
gcry_mac_open (gcry_mac_hd_t * h, int algo, unsigned int flags,
               gcry_ctx_t ctx)
{
  gcry_err_code_t err;
  gcry_mac_hd_t hd = NULL;

  if ((flags & ~GCRY_MAC_FLAG_SECURE))
    err = GPG_ERR_INV_ARG;
  else
    err = mac_open (&hd, algo, !!(flags & GCRY_MAC_FLAG_SECURE), ctx);

  *h = err ? NULL : hd;
  return gpg_error (err);
}


void
gcry_mac_close (gcry_mac_hd_t hd)
{
  mac_close (hd);
}


gcry_error_t
gcry_mac_setkey (gcry_mac_hd_t hd, const void *key, size_t keylen)
{
  gcry_error_t err;

  err = mac_setkey (hd, key, keylen);

  return gpg_error (err);
}


gcry_error_t
gcry_mac_setiv (gcry_mac_hd_t hd, const void *iv, size_t ivlen)
{
  gcry_error_t err;

  err = mac_setiv (hd, iv, ivlen);

  return gpg_error (err);
}


gcry_error_t
gcry_mac_write (gcry_mac_hd_t hd, const void *inbuf, size_t inlen)
{
  gcry_err_code_t err;

  err = mac_write (hd, inbuf, inlen);

  return gpg_error (err);
}


gcry_error_t
gcry_mac_read (gcry_mac_hd_t hd, void *outbuf, size_t * outlen)
{
  gcry_error_t err;

  err = mac_read (hd, outbuf, outlen);

  return gpg_error (err);
}


gcry_error_t
gcry_mac_verify (gcry_mac_hd_t hd, const void *buf, size_t buflen)
{
  gcry_err_code_t err;

  err = mac_verify (hd, buf, buflen);

  return gpg_error (err);
}


unsigned int
gcry_mac_get_algo_maclen (int algo)
{
  gcry_mac_spec_t *spec;

  spec = spec_from_algo (algo);
  if (!spec || !spec->ops || !spec->ops->get_maclen)
    return 0;

  return spec->ops->get_maclen (algo);
}


unsigned int
gcry_mac_get_algo_keylen (int algo)
{
  gcry_mac_spec_t *spec;

  spec = spec_from_algo (algo);
  if (!spec || !spec->ops || !spec->ops->get_keylen)
    return 0;

  return spec->ops->get_keylen (algo);
}


gcry_error_t
gcry_mac_ctl (gcry_mac_hd_t hd, int cmd, void *buffer, size_t buflen)
{
  gcry_err_code_t rc = GPG_ERR_NO_ERROR;

  /* Currently not used.  */
  (void) hd;
  (void) buffer;
  (void) buflen;

  switch (cmd)
    {
    case GCRYCTL_RESET:
      rc = mac_reset (hd);
      break;
    default:
      rc = GPG_ERR_INV_OP;
    }
  return gcry_error (rc);
}


/* Return information about the given MAC algorithm ALGO.

    GCRYCTL_TEST_ALGO:
        Returns 0 if the specified algorithm ALGO is available for use.
        BUFFER and NBYTES must be zero.

   Note: Because this function is in most cases used to return an
   integer value, we can make it easier for the caller to just look at
   the return value.  The caller will in all cases consult the value
   and thereby detecting whether a error occurred or not (i.e. while
   checking the block size)
 */
gcry_error_t
gcry_mac_algo_info (int algo, int what, void *buffer, size_t * nbytes)
{
  gcry_err_code_t err = GPG_ERR_NO_ERROR;
  unsigned int ui;

  switch (what)
    {
    case GCRYCTL_GET_KEYLEN:
      if (buffer || (!nbytes))
        err = GPG_ERR_INV_ARG;
      else
        {
          ui = gcry_mac_get_algo_keylen (algo);
          if (ui > 0)
            *nbytes = (size_t) ui;
          else
            /* The only reason for an error is an invalid algo.  */
            err = GPG_ERR_MAC_ALGO;
        }
      break;
    case GCRYCTL_TEST_ALGO:
      if (buffer || nbytes)
        err = GPG_ERR_INV_ARG;
      else
        err = check_mac_algo (algo);
      break;

    default:
      err = GPG_ERR_INV_OP;
    }

  return gcry_error (err);
}