/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/vdev_raidz_impl.h>

/*
 * Provide native cpu scalar routines.
 * Support 32bit and 64bit CPUs. Check the size of unsigned long long
 * to determine native register size .
 */

#if (((~(0ULL)) >> 24) == 0xFF)
#define	RAIDZ_ELEM_SIZE (4)
#elif (((~(0ULL)) >> 56) == 0xFF)
#define	RAIDZ_ELEM_SIZE (8)
#else
#error "VDEV_RAIDZ: Native CPU register size not supported."
#endif

typedef union elem {
	unsigned long long e;
	unsigned char b[RAIDZ_ELEM_SIZE];
} elem_t __attribute__((aligned(RAIDZ_ELEM_SIZE)));

static const struct raidz_scalar_constants {
	elem_t mod;
	elem_t mask;
	elem_t check;
} raidz_scalar_constants = {
#if RAIDZ_ELEM_SIZE == 8
	.mod.e   = 0x1d1d1d1d1d1d1d1dULL,
	.mask.e  = 0xfefefefefefefefeULL,
	.check.e = 0x8080808080808080ULL,
#else
	.mod.e   = 0x1d1d1d1dULL,
	.mask.e  = 0xfefefefeULL,
	.check.e = 0x80808080ULL,
#endif
};

/* Keep multiplication in log form for scalar */
raidz_inline static int
fix_mul_exponent(int e) {
	while (e > 255)
		e -= 255;
	return (e);
}

static raidz_inline void
xor_acc_x2(const elem_t *data, elem_t *a0, elem_t *a1)
{
	a0->e ^= data[0].e;
	a1->e ^= data[1].e;
}

static raidz_inline void
load_x2(const elem_t *src, elem_t *a0, elem_t *a1)
{
	a0->e = src[0].e;
	a1->e = src[1].e;
}

static raidz_inline void
store_x2(elem_t *dst, elem_t a0, elem_t a1)
{
	dst[0].e = a0.e;
	dst[1].e = a1.e;
}

static raidz_inline void
zero_store_x2(elem_t *dst)
{
	dst[0].e = dst[1].e = 0;
}

static raidz_inline void
xor_x2(const elem_t a0, const elem_t a1, elem_t *r0, elem_t *r1)
{
	r0->e ^= a0.e;
	r1->e ^= a1.e;
}

static raidz_inline void
copy_x2(const elem_t a0, const elem_t a1, elem_t *c0, elem_t *c1)
{
	c0->e = a0.e;
	c1->e = a1.e;
}

static raidz_inline void
zero_x2(elem_t *r0, elem_t *r1)
{
	r0->e = r1->e = 0ULL;
}

static raidz_inline void
mul2_x2(elem_t *a0, elem_t *a1)
{
	elem_t cmp, dbl, mask;

	cmp.e = (*a0).e & raidz_scalar_constants.check.e;
	mask.e = (cmp.e << 1) - (cmp.e >> 7);
	dbl.e = ((*a0).e << 1) & raidz_scalar_constants.mask.e;
	(*a0).e = dbl.e ^ (mask.e & raidz_scalar_constants.mod.e);

	cmp.e = (*a1).e & raidz_scalar_constants.check.e;
	mask.e = (cmp.e << 1) - (cmp.e >> 7);
	dbl.e = ((*a1).e << 1) & raidz_scalar_constants.mask.e;
	(*a1).e = dbl.e ^ (mask.e & raidz_scalar_constants.mod.e);
}

static raidz_inline void
mul4_x2(elem_t *a0, elem_t *a1)
{
	mul2_x2(a0, a1);
	mul2_x2(a0, a1);
}

static raidz_inline elem_t
exp2_x1(const elem_t a, const int mul)
{
	elem_t r;
	switch (RAIDZ_ELEM_SIZE) {
		case 8:
			r.b[7] = vdev_raidz_exp2(a.b[7], mul);
			r.b[6] = vdev_raidz_exp2(a.b[6], mul);
			r.b[5] = vdev_raidz_exp2(a.b[5], mul);
			r.b[4] = vdev_raidz_exp2(a.b[4], mul);
		case 4:
			r.b[3] = vdev_raidz_exp2(a.b[3], mul);
			r.b[2] = vdev_raidz_exp2(a.b[2], mul);
			r.b[1] = vdev_raidz_exp2(a.b[1], mul);
			r.b[0] = vdev_raidz_exp2(a.b[0], mul);
	}
	return (r);
}

static raidz_inline void
exp2_x2(elem_t *a0, elem_t *a1, const int mul)
{
	*a0 = exp2_x1(*a0, mul);
	*a1 = exp2_x1(*a1, mul);
}

static raidz_inline void
raidz_flush_data(void)
{
}

#define	D(idx) ((elem_t *)(((uint8_t *)cols[idx].rc_data) + off))

static raidz_inline size_t
gen_p_block(raidz_col_t *cols, const size_t nd, const size_t off)
{
	size_t c;
	elem_t p0, p1;

	load_x2(D(CODE_P+1), &p0, &p1);
	for (c = 2; c < nd; c++) {
		xor_acc_x2(D(c), &p0, &p1);
	}
	store_x2(D(CODE_P), p0, p1);
	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
gen_pq_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off)
{
	size_t c;
	elem_t p0, p1;
	elem_t q0, q1;

	load_x2(D(CODE_Q+1), &p0, &p1);
	copy_x2(p0, p1, &q0, &q1);
	for (c = 3; c < nld; c++) {
		mul2_x2(&q0, &q1);
		xor_acc_x2(D(c), &p0, &p1);
		xor_acc_x2(D(c), &q0, &q1);
	}
	store_x2(D(CODE_P), p0, p1);

	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
	}
	store_x2(D(CODE_Q), q0, q1);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
gen_pqr_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off)
{
	size_t c;
	elem_t p0, p1;
	elem_t q0, q1;
	elem_t r0, r1;

	load_x2(D(CODE_R+1), &p0, &p1);
	copy_x2(p0, p1, &q0, &q1);
	copy_x2(p0, p1, &r0, &r1);

	for (c = 4; c < nld; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
		xor_acc_x2(D(c), &p0, &p1);
		xor_acc_x2(D(c), &q0, &q1);
		xor_acc_x2(D(c), &r0, &r1);
	}
	store_x2(D(CODE_P), p0, p1);

	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
	}
	store_x2(D(CODE_Q), q0, q1);
	store_x2(D(CODE_R), r0, r1);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_p_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const size_t nd, const size_t off)
{
	size_t c;
	elem_t p0, p1;

	zero_store_x2(D(xidx));
	load_x2(D(parity), &p0, &p1);

	for (c = parity+1; c < nd; c++) {
		xor_acc_x2(D(c), &p0, &p1);
	}
	xor_acc_x2(D(CODE_P), &p0, &p1);
	store_x2(D(xidx), p0, p1);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_q_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t q0, q1;

	zero_store_x2(D(xidx));
	load_x2(D(parity), &q0, &q1);

	for (c = parity+1; c < nld; c++) {
		mul2_x2(&q0, &q1);
		xor_acc_x2(D(c), &q0, &q1);
	}
	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
	}

	xor_acc_x2(D(CODE_Q), &q0, &q1);
	exp2_x2(&q0, &q1, x_mul);
	store_x2(D(xidx), q0, q1);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_r_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t r0, r1;

	zero_store_x2(D(xidx));
	load_x2(D(parity), &r0, &r1);

	for (c = parity+1; c < nld; c++) {
		mul4_x2(&r0, &r1);
		xor_acc_x2(D(c), &r0, &r1);
	}
	for (; c < nd; c++) {
		mul4_x2(&r0, &r1);
	}
	xor_acc_x2(D(CODE_R), &r0, &r1);
	exp2_x2(&r0, &r1, x_mul);

	store_x2(D(xidx), r0, r1);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pq_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t xsize, const size_t ysize,
	const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t p0, p1;
	elem_t q0, q1;
	elem_t t0, t1;

	zero_x2(&p0, &p1);
	zero_x2(&q0, &q1);

	for (c = parity; c < nld; c++) {
		mul2_x2(&q0, &q1);
		if (c != x && c != y) {
			xor_acc_x2(D(c), &p0, &p1);
			xor_acc_x2(D(c), &q0, &q1);
		}
	}
	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
	}

	xor_acc_x2(D(CODE_P), &p0, &p1);
	xor_acc_x2(D(CODE_Q), &q0, &q1);

	/* Save Pxy */
	copy_x2(p0, p1, &t0, &t1);

	/* Calc X */
	exp2_x2(&p0, &p1, a_mul);
	exp2_x2(&q0, &q1, b_mul);
	xor_x2(q0, q1, &p0, &p1);
	store_x2(D(x), p0, p1);

	if (xsize == ysize) {
		/* Calc Y */
		xor_x2(t0, t1, &p0, &p1);
		store_x2(D(y), p0, p1);
	}

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t xsize,
	const size_t ysize, const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t p0, p1;
	elem_t r0, r1;
	elem_t t0, t1;

	zero_x2(&p0, &p1);
	zero_x2(&r0, &r1);

	for (c = parity; c < nld; c++) {
		mul4_x2(&r0, &r1);
		if (c != x && c != y) {
			xor_acc_x2(D(c), &p0, &p1);
			xor_acc_x2(D(c), &r0, &r1);
		}
	}
	for (; c < nd; c++) {
		mul4_x2(&r0, &r1);
	}

	xor_acc_x2(D(CODE_P), &p0, &p1);
	xor_acc_x2(D(CODE_R), &r0, &r1);

	/* Save Pxy */
	copy_x2(p0, p1, &t0, &t1);

	/* Calc X */
	exp2_x2(&p0, &p1, a_mul);
	exp2_x2(&r0, &r1, b_mul);
	xor_x2(r0, r1, &p0, &p1);
	store_x2(D(x), p0, p1);

	if (xsize == ysize) {
		/* Calc Y */
		xor_x2(t0, t1, &p0, &p1);
		store_x2(D(y), p0, p1);
	}

	return (2 * sizeof (elem_t));
}


static raidz_inline size_t
rec_qr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t xsize,
	const size_t ysize, const int xq_mul, const int x_mul,
	const int yq_mul, const int y_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t q0, q1;
	elem_t r0, r1;
	elem_t t0, t1;

	zero_x2(&q0, &q1);
	zero_x2(&r0, &r1);

	for (c = parity; c < nld; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
		if (c != x && c != y) {
			xor_acc_x2(D(c), &q0, &q1);
			xor_acc_x2(D(c), &r0, &r1);
		}
	}
	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
	}

	xor_acc_x2(D(CODE_Q), &q0, &q1);
	xor_acc_x2(D(CODE_R), &r0, &r1);

	/* Calc X */
	copy_x2(q0, q1, &t0, &t1);
	exp2_x2(&t0, &t1, xq_mul); /* X = Q * xqm */
	xor_x2(r0, r1, &t0, &t1); /* X = R ^ X   */
	exp2_x2(&t0, &t1, x_mul);  /* X = X * xm  */
	store_x2(D(x), t0, t1);

	if (xsize == ysize) {
		/* Calc Y */
		exp2_x2(&q0, &q1, yq_mul);
		xor_x2(r0, r1, &q0, &q1);
		exp2_x2(&q0, &q1, y_mul);
		store_x2(D(y), q0, q1);
	}

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pqr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t z,
	const size_t xsize, const size_t ysize, const size_t zsize,
	const int xp_mul, const int xq_mul, const int xr_mul,
	const int yp_mul, const int yq_mul, const int upd_q_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;
	elem_t p0, p1, q0, q1, r0, r1;
	elem_t t0, t1, t2, t3;

	zero_x2(&p0, &p1);
	zero_x2(&q0, &q1);
	zero_x2(&r0, &r1);

	for (c = parity; c < nld; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
		if (c != x && c != y && c != z) {
			xor_acc_x2(D(c), &p0, &p1);
			xor_acc_x2(D(c), &q0, &q1);
			xor_acc_x2(D(c), &r0, &r1);
		}
	}
	for (; c < nd; c++) {
		mul2_x2(&q0, &q1);
		mul4_x2(&r0, &r1);
	}

	xor_acc_x2(D(CODE_P), &p0, &p1);
	xor_acc_x2(D(CODE_Q), &q0, &q1);
	xor_acc_x2(D(CODE_R), &r0, &r1);

	/* Calc X */
	copy_x2(p0, p1, &t0, &t1);
	exp2_x2(&t0, &t1, xp_mul);
	copy_x2(q0, q1, &t2, &t3);
	exp2_x2(&t2, &t3, xq_mul);
	xor_x2(t2, t3, &t0, &t1);
	exp2_x2(&r0, &r1, xr_mul);
	xor_x2(r0, r1, &t0, &t1);
	store_x2(D(x), t0, t1);


	if (ysize == xsize) {
		/* Calc Y */
		xor_x2(t0, t1, &p0, &p1);
		exp2_x2(&t0, &t1, upd_q_mul);
		xor_x2(t0, t1, &q0, &q1);

		copy_x2(p0, p1, &t0, &t1);
		exp2_x2(&t0, &t1, yp_mul);
		exp2_x2(&q0, &q1, yq_mul);
		xor_x2(q0, q1, &t0, &t1);
		store_x2(D(y), t0, t1);

		if (zsize == xsize) {
			/* Calc Z */
			xor_x2(p0, p1, &t0, &t1);
			store_x2(D(z), t0, t1);
		}
	}

	return (2 * sizeof (elem_t));
}


#include "vdev_raidz_math_impl.h"

static boolean_t
raidz_math_will_scalar_work(void)
{
	return (B_TRUE); /* will always work */
}

RAIDZ_SCALAR_GEN_WRAP(p, scalar);
RAIDZ_SCALAR_GEN_WRAP(pq, scalar);
RAIDZ_SCALAR_GEN_WRAP(pqr, scalar);

RAIDZ_SCALAR_REC_WRAP(p, scalar);
RAIDZ_SCALAR_REC_WRAP(q, scalar);
RAIDZ_SCALAR_REC_WRAP(r, scalar);
RAIDZ_SCALAR_REC_WRAP(pq, scalar);
RAIDZ_SCALAR_REC_WRAP(pr, scalar);
RAIDZ_SCALAR_REC_WRAP(qr, scalar);
RAIDZ_SCALAR_REC_WRAP(pqr, scalar);

const raidz_math_ops_t vdev_raidz_scalar_impl = {
	.gen = { &scalar_gen_p, &scalar_gen_pq, &scalar_gen_pqr },
	.rec = {
		&scalar_rec_p, &scalar_rec_q, &scalar_rec_r,
		&scalar_rec_pq, &scalar_rec_pr, &scalar_rec_qr,
		&scalar_rec_pqr
	},
	.is_supported = &raidz_math_will_scalar_work,
	.name = "scalar"
};

/* Powers of 2 in the Galois field defined above. */
const unsigned char vdev_raidz_pow2[511] __attribute__((aligned(256))) = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
	0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
	0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
	0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
	0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
	0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
	0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
	0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
	0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
	0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
	0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
	0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
	0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
	0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
	0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
	0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
	0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
	0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
	0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
	0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
	0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
	0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
	0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
	0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
	0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
	0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
	0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
	0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
	0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
	0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01,
	0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
	0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
	0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
	0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
	0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
	0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
	0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
	0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
	0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
	0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
	0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
	0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
	0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
	0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
	0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
	0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
	0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
	0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
	0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
	0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
	0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
	0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
	0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
	0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
	0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
	0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
	0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
	0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
	0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
	0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01
};

/* Logs of 2 in the Galois field defined above. */
const unsigned char vdev_raidz_log2[256] __attribute__((aligned(256))) = {
	0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6,
	0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
	0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81,
	0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
	0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21,
	0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
	0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9,
	0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
	0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd,
	0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
	0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd,
	0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
	0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e,
	0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
	0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b,
	0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
	0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d,
	0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
	0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c,
	0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
	0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd,
	0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
	0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e,
	0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
	0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76,
	0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
	0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa,
	0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
	0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51,
	0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
	0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8,
	0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};
