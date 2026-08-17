/*
    Copyright (C) 2018 MariaDB Corporation AB

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc.,
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA
*/
#include <ma_crypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

static gnutls_digest_algorithm_t ma_hash_get_algorithm(unsigned int alg)
{
  switch(alg)
  {
  case MA_HASH_MD5:
    return GNUTLS_DIG_MD5;
  case MA_HASH_SHA1:
    return GNUTLS_DIG_SHA1;
  case MA_HASH_SHA256:
    return GNUTLS_DIG_SHA256;
  case MA_HASH_SHA384:
    return GNUTLS_DIG_SHA384;
  case MA_HASH_SHA512:
    return GNUTLS_DIG_SHA512;
  case MA_HASH_RIPEMD160:
    return GNUTLS_DIG_RMD160;
  default:
    return GNUTLS_DIG_UNKNOWN;
  }
}

static MA_HASH_CTX *ma_hash_new_gnutls(unsigned int algorithm, MA_HASH_CTX *unused_ctx __attribute__((unused)))
{
  gnutls_hash_hd_t ctx= NULL;
  gnutls_digest_algorithm_t hash_alg= ma_hash_get_algorithm(algorithm);

  /* unknown or unsupported hash algorithm */
  if (hash_alg == GNUTLS_DIG_UNKNOWN)
    return NULL;

  if (gnutls_hash_init(&ctx, hash_alg) < 0)
    return NULL;

  return (MA_HASH_CTX *)ctx;
}

static void ma_hash_free_gnutls(MA_HASH_CTX *ctx)
{
  if (ctx)
    gnutls_hash_deinit((gnutls_hash_hd_t)ctx, NULL);
}

static void ma_hash_input_gnutls(MA_HASH_CTX *ctx, const unsigned char *buffer, size_t len)
{
  gnutls_hash((gnutls_hash_hd_t)ctx, (const void *)buffer, len);
}

static void ma_hash_result_gnutls(MA_HASH_CTX *ctx, unsigned char *digest)
{
  gnutls_hash_output((gnutls_hash_hd_t)ctx, digest);
}


static MA_HASH_CTX *ma_hash_new_sm3(unsigned int algorithm, MA_HASH_CTX *ctx)
{
  MA_HASH_CTX *newctx= ctx;
  if (algorithm != MA_HASH_SM3) {
    return NULL;
  }
  if (!newctx)
  {
    newctx = (MA_HASH_CTX *)calloc(1, sizeof(MA_HASH_CTX));
    if (NULL == newctx)
      goto error;
  }
  else
    memset(newctx, 0, sizeof(MA_HASH_CTX));

  newctx->algorithm = MA_HASH_SM3;
  newctx->ctx = (char*)newctx + sizeof(MA_HASH_CTX);
  if (NULL == newctx->ctx)
    goto error;
  ob_sm3_starts((ob_sm3_context*)(newctx->ctx));
  return newctx;
error:
  if (newctx && !ctx)
    free(newctx);
  return NULL;
}
static void ma_hash_free_sm3(MA_HASH_CTX *ctx)
{
  if (ctx)
  {
    if (ctx->ctx) {
      free(ctx->ctx);
      ctx->ctx = NULL;
	}
    free(ctx);
  }
}

static void ma_hash_input_sm3(MA_HASH_CTX *ctx, const unsigned char *buffer, size_t len)
{
  if (ctx && ctx->ctx) {
    ob_sm3_context* context = (ob_sm3_context*)ctx->ctx;
    ob_sm3_update( context, buffer, len );
  }
}
static void ma_hash_result_sm3(MA_HASH_CTX *ctx, unsigned char *digest)
{
  if (ctx && ctx->ctx) {
    ob_sm3_context* context = (ob_sm3_context*)ctx->ctx;
    ob_sm3_finish( context, digest);
  }
}


//------------------------------------------------------------------------------------
MA_HASH_CTX *ma_hash_new(unsigned int algorithm, MA_HASH_CTX *ctx)
{
  if (algorithm == MA_HASH_SM3) {
    return ma_hash_new_sm3(algorithm, ctx);
  } else {
    return ma_hash_new_gnutls(algorithm, ctx);
  }
}
void ma_hash_free(MA_HASH_CTX *ctx)
{
  if (ctx && ctx->algorithm == MA_HASH_SM3) {
    ma_hash_free_sm3(ctx);
  } else {
    ma_hash_free_gnutls(ctx);
  }
}

void ma_hash_input(MA_HASH_CTX *ctx, const unsigned char *buffer, size_t len)
{
  if (ctx && ctx->algorithm == MA_HASH_SM3) {
    ma_hash_input_sm3(ctx, buffer, len);
  } else {
    ma_hash_input_gnutls(ctx, buffer, len);
  }
}

void ma_hash_result(MA_HASH_CTX *ctx, unsigned char *digest)
{
  if (ctx && ctx->algorithm == MA_HASH_SM3) {
    ma_hash_result_sm3(ctx, digest);
  } else {
    ma_hash_result_gnutls(ctx, digest);
  }
}
