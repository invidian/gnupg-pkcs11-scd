/*
 * Copyright (c) 2006 Zeljko Vrba <zvrba@globalnet.hr>
 * Copyright (c) 2006 Alon Bar-Lev <alon.barlev@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modifi-
 * cation, are permitted provided that the following conditions are met:
 *
 *   o  Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   o  Redistributions in binary form must reproduce the above copyright no-
 *      tice, this list of conditions and the following disclaimer in the do-
 *      cumentation and/or other materials provided with the distribution.
 *
 *   o  The names of the contributors may not be used to endorse or promote
 *      products derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LI-
 * ABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUEN-
 * TIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEV-
 * ER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABI-
 * LITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "common.h"
#if defined(USE_GNUTLS)
#include <gnutls/x509.h>
#endif
#if defined(USE_OPENSSL)
#include <openssl/x509.h>
#endif
#include "encoding.h"

#if defined(USE_OPENSSL)
#if OPENSSL_VERSION_NUMBER < 0x00908000L
typedef unsigned char *my_openssl_d2i_t;
#else
typedef const unsigned char *my_openssl_d2i_t;
#endif
#endif

/**
   Convert X.509 RSA public key into gcrypt internal sexp form. Only RSA
   public keys are accepted at the moment. The resul is stored in *sexp,
   which must be freed (using ) when not needed anymore. *sexp must be
   NULL on entry, since it is overwritten.
*/
gpg_err_code_t
keyutil_get_cert_sexp (
	unsigned char *der,
	size_t len,
	gcry_sexp_t *p_sexp
) {
	gpg_err_code_t error = GPG_ERR_NO_ERROR;
	gcry_mpi_t n_mpi = NULL, e_mpi = NULL;
	gcry_sexp_t sexp = NULL;

#if defined(USE_GNUTLS)

	gnutls_x509_crt_t cert = NULL;
	gnutls_datum_t datum = {der, len};
	gnutls_datum_t m, e;

	if (
		error == GPG_ERR_NO_ERROR &&
		gnutls_x509_crt_init (&cert) != GNUTLS_E_SUCCESS
	) {
		cert = NULL;
		error = GPG_ERR_ENOMEM;
	}

	if (
		error == GPG_ERR_NO_ERROR &&
		gnutls_x509_crt_import (cert, &datum, GNUTLS_X509_FMT_DER) != GNUTLS_E_SUCCESS
	) {
		error = GPG_ERR_BAD_CERT;
	}

	if (
		error == GPG_ERR_NO_ERROR &&
		gnutls_x509_crt_get_pk_rsa_raw (cert, &m, &e) != GNUTLS_E_SUCCESS
	) {
		error = GPG_ERR_BAD_KEY;
		m.data = NULL;
		e.data = NULL;
	}

	if (
		error == GPG_ERR_NO_ERROR &&
		( 
			gcry_mpi_scan(&n_mpi, GCRYMPI_FMT_USG, m.data, m.size, NULL) ||
			gcry_mpi_scan(&e_mpi, GCRYMPI_FMT_USG, e.data, e.size, NULL)
		)
	) {
		error = GPG_ERR_BAD_KEY;
	}

#elif defined(USE_OPENSSL)

	X509 *x509 = NULL;
	EVP_PKEY *pubkey = NULL;
	char *n_hex = NULL, *e_hex = NULL;

	if (
		error == GPG_ERR_NO_ERROR &&
		!d2i_X509 (&x509, (my_openssl_d2i_t *)&der, len)
	) {
		error = GPG_ERR_BAD_CERT;
	}
 
	if (
		error == GPG_ERR_NO_ERROR &&
		(pubkey = X509_get_pubkey (x509)) == NULL
	) {
		return GPG_ERR_BAD_CERT;
	}
 
	if (
		error == GPG_ERR_NO_ERROR &&
		pubkey->type != EVP_PKEY_RSA
	) {
		error = GPG_ERR_WRONG_PUBKEY_ALGO;
	}
	
	if (error == GPG_ERR_NO_ERROR) {
		n_hex = BN_bn2hex(pubkey->pkey.rsa->n);
		e_hex = BN_bn2hex(pubkey->pkey.rsa->e);
		
		if(n_hex == NULL || e_hex == NULL) {
			error = GPG_ERR_BAD_KEY;
		}
	}
 
	if (
		error == GPG_ERR_NO_ERROR &&
		( 
			gcry_mpi_scan(&n_mpi, GCRYMPI_FMT_HEX, n_hex, 0, NULL) ||
			gcry_mpi_scan(&e_mpi, GCRYMPI_FMT_HEX, e_hex, 0, NULL)
		)
	) {
		error = GPG_ERR_BAD_KEY;
	}
#else
#error Invalid configuration.
#endif

	if (
		error == GPG_ERR_NO_ERROR &&
		gcry_sexp_build(
			&sexp,
			NULL,
			"(public-key (rsa (n %m) (e %m)))",
			n_mpi,
			e_mpi
		)
	) {
		error = GPG_ERR_BAD_KEY;
	}

	if (error == GPG_ERR_NO_ERROR) {
		*p_sexp = sexp;
		sexp = NULL;
	}

	if (n_mpi != NULL) {
		gcry_mpi_release(n_mpi);
		n_mpi = NULL;
	}

	if (e_mpi != NULL) {
		gcry_mpi_release (e_mpi);
		e_mpi = NULL;
	}

#if defined(USE_GNUTLS)

	if (m.data != NULL) {
		gnutls_free (m.data);
		m.data = NULL;
	}

	if (e.data != NULL) {
		gnutls_free (e.data);
		e.data = NULL;
	}

	if (cert != NULL) {
		gnutls_x509_crt_deinit (cert);
		cert = NULL;
	}

#elif defined(USE_OPENSSL)

	if (x509 != NULL) {
		X509_free (x509);
		x509 = NULL;
	}

	if (pubkey != NULL) {
		EVP_PKEY_free(pubkey);
		pubkey = NULL;
	}

	if (n_hex != NULL) {
		OPENSSL_free (n_hex);
		n_hex = NULL;
	}

	if (e_hex != NULL) {
		OPENSSL_free (e_hex);
		e_hex = NULL;
	}

#else
#error Invalid configuration.
#endif

	if (sexp != NULL) {
		gcry_sexp_release(sexp);
		sexp = NULL;
	}

	return error;
}

#if 0
/**
   Calculate certid for the certificate. The certid is stored as hex-encoded,
   null-terminated string into certid which must be at least 41 bytes long.
   This is very primitive ID, just using the SHA1 of the whole certificate DER
   encoding. Currently not used.
*/
void cert_get_hexgrip(unsigned char *der, size_t len, char *certid)
{
	int ret;
	char grip[20];

	SHA1(der, len, grip);
	ret = bin2hex(hexgrip, 41, grip, 20);
	g_assert(ret == 20);
}
#endif

/** Calculate hex-encoded keygrip of public key in sexp. */
char *keyutil_get_cert_hexgrip (gcry_sexp_t sexp)
{
	char *ret = NULL;
	unsigned char grip[20];
	
	if (gcry_pk_get_keygrip (sexp, grip)) {
		ret = encoding_bin2hex(grip, sizeof (grip));
	}

	return ret;
}