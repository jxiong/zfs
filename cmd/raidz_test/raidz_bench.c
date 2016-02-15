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

#include <sys/zfs_context.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/zio.h>
#include <sys/vdev_raidz_impl.h>
#include <stdio.h>

#include <sys/time.h>
#include <sys/resource.h>

#include "raidz_test.h"

#define	GEN_BENCH_MEMORY	(1ULL<<32)
#define	REC_BENCH_MEMORY	(1ULL<<30)

static zio_t zio_bench;
static raidz_map_t *rm_bench;
static size_t max_data_size;

static void
bench_init_raidz_map(void)
{
	max_data_size = (1ULL<<24) * rto_opts.rto_dcols;

	if (rto_opts.rto_verbose) {
		PRINT(DBLSEP);
		PRINT("Initializing benchmark data ...\n\n");
	}

	zio_bench.io_offset = 0;
	zio_bench.io_size = max_data_size;

	/*
	 * To permit larger column sizes these have to be done
	 * allocated using aligned alloc instead of zio_data_buf_alloc
	 */
	zio_bench.io_data = umem_alloc_aligned(max_data_size, 512, UMEM_NOFAIL);

	init_zio_data(&zio_bench);
}

static void
bench_fini_raidz_maps(void)
{
	/* tear down golden zio */
	umem_free(zio_bench.io_data, max_data_size);
	bzero(&zio_bench, sizeof (zio_t));
}

static double
get_time_diff(struct rusage *start, struct rusage *stop)
{
	return (((double)stop->ru_utime.tv_sec*1e6 +
		(double)stop->ru_utime.tv_usec) -
		((double)start->ru_utime.tv_sec*1e6 +
		(double)start->ru_utime.tv_usec)) / 1e6;
}

static void
run_gen_bench(void)
{
	raidz_math_ops_t *c_ops;
	struct rusage start, stop;
	size_t impl, fn, ds, iter_cnt, iter;
	double elapsed, d_bw;

	if (rto_opts.rto_verbose) {
		PRINT(DBLSEP);
		PRINT("Benchmarking parity generation...\n\n");
	}

	PRINT("impl, math, dcols, dsize, disk_bw, total_bw, iter\n");

	/* benchmark original impl */
	zfs_raidz_math_impl_set("original", NULL);

	/* Benchmark generate functions */
	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {

		for (ds = 9; ds <= MAX_DCSIZE; ds++) {

			/* create suitable raidz_map */
			zio_bench.io_size = (1ULL<<ds) * rto_opts.rto_dcols;
			rm_bench = vdev_raidz_map_alloc(&zio_bench, 9,
				rto_opts.rto_dcols + fn+1, fn+1);

			/* guess iteration count */
			iter_cnt = (GEN_BENCH_MEMORY) / zio_bench.io_size;

			getrusage(RUSAGE_THREAD, &start);
			for (iter = 0; iter < iter_cnt; iter++) {
				vdev_raidz_generate_parity(rm_bench);
			}
			getrusage(RUSAGE_THREAD, &stop);

			elapsed = get_time_diff(&start, &stop);
			d_bw = (double)iter_cnt * (double)(1ULL<<ds);
			d_bw /= (1024. * 1024. * elapsed);

			PRINT("%10s, %8s, %zu, %10llu, %lf, %lf, %zu\n",
				"original",
				raidz_gen_name[fn],
				rto_opts.rto_dcols,
				(1ULL<<ds),
				d_bw,
				d_bw * (double)(rto_opts.rto_dcols+fn+1),
				iter_cnt);

			vdev_raidz_map_free(rm_bench);
		}
	}

	/* benchmark new impl */
	for (impl = 0; impl < raidz_supp_maths_cnt; impl++) {
		c_ops = raidz_supp_maths[impl];

		for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {

			for (ds = 9; ds <= MAX_DCSIZE; ds++) {

				/* create suitable raidz_map */
				zio_bench.io_size = (1ULL<<ds);
				zio_bench.io_size *= rto_opts.rto_dcols;
				rm_bench = vdev_raidz_map_alloc(&zio_bench, 9,
					rto_opts.rto_dcols + fn+1, fn+1);

				/* guess iteration count */
				iter_cnt = (GEN_BENCH_MEMORY) /
					zio_bench.io_size;

				getrusage(RUSAGE_THREAD, &start);
				for (iter = 0; iter < iter_cnt; iter++) {
					c_ops->gen[fn](rm_bench);
				}
				getrusage(RUSAGE_THREAD, &stop);

				elapsed = get_time_diff(&start, &stop);
				d_bw = (double)iter_cnt * (double)(1ULL<<ds);
				d_bw /= (1024. * 1024. * elapsed);

				PRINT("%10s, %8s, %zu, %10llu, %lf, %lf, %zu\n",
					c_ops->name,
					raidz_gen_name[fn],
					rto_opts.rto_dcols,
					(1ULL<<ds),
					d_bw,
					d_bw * (rto_opts.rto_dcols+fn+1),
					iter_cnt);
			}
		}
	}
}

static void
run_rec_bench(void)
{
	raidz_math_ops_t *c_ops;
	struct rusage start, stop;
	size_t impl, fn, ds, iter_cnt, iter;
	double elapsed, d_bw;
	int tgx[3] = { 3, 4, 5};
	size_t rec_disks[7] = {1, 1, 1, 2, 2, 2, 3};
	int bench_rec_orig[7][3] = {
		{1, 2, 3},
		{0, 2, 3},
		{0, 1, 3},
		{2, 3, 4},
		{1, 3, 4},
		{0, 3, 4},
		{3, 4, 5}
	};

	if (rto_opts.rto_verbose) {
		PRINT(DBLSEP);
		PRINT("Benchmarking data reconstruction...\n\n");
	}

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {
		for (ds = 9; ds <= MAX_DCSIZE; ds++) {
			/* create suitable raidz_map */
			zio_bench.io_size = (1ULL<<ds) * rto_opts.rto_dcols;
			rm_bench = vdev_raidz_map_alloc(&zio_bench, 9,
				rto_opts.rto_dcols + CODE_PQR, CODE_PQR);

			/* guess iteration count */
			iter_cnt = (REC_BENCH_MEMORY) / zio_bench.io_size;

			getrusage(RUSAGE_THREAD, &start);
			for (iter = 0; iter < iter_cnt; iter++) {
				vdev_raidz_reconstruct(rm_bench,
					bench_rec_orig[fn], 3);
			}
			getrusage(RUSAGE_THREAD, &stop);

			elapsed = get_time_diff(&start, &stop);
			d_bw = (double)iter_cnt * (double)(1ULL<<ds);
			d_bw /= (1024. * 1024. * elapsed);

			PRINT("%10s, %8s, %zu, %10llu, %lf, %lf, %zu\n",
				"original",
				raidz_rec_name[fn],
				rto_opts.rto_dcols,
				(1ULL<<ds),
				d_bw,
				d_bw * (rto_opts.rto_dcols+rec_disks[fn]),
				iter_cnt);

			vdev_raidz_map_free(rm_bench);
		}
	}

	for (impl = 0; impl < raidz_supp_maths_cnt; impl++) {
		c_ops = raidz_supp_maths[impl];

		for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {
			for (ds = 9; ds <= MAX_DCSIZE; ds++) {
				/* create suitable raidz_map */
				zio_bench.io_size = (1ULL<<ds) *
					rto_opts.rto_dcols;
				rm_bench = vdev_raidz_map_alloc(&zio_bench, 9,
					rto_opts.rto_dcols + CODE_PQR,
					CODE_PQR);

				/* guess iteration count */
				iter_cnt = (REC_BENCH_MEMORY) /
					zio_bench.io_size;

				getrusage(RUSAGE_THREAD, &start);
				for (iter = 0; iter < iter_cnt; iter++) {
					c_ops->rec[fn](rm_bench, tgx);
				}
				getrusage(RUSAGE_THREAD, &stop);

				elapsed = get_time_diff(&start, &stop);
				d_bw = (double)iter_cnt * (double)(1ULL<<ds);
				d_bw /= (1024. * 1024. * elapsed);

				PRINT("%10s, %8s, %zu, %10llu, %lf, %lf, %zu\n",
					c_ops->name,
					raidz_rec_name[fn],
					rto_opts.rto_dcols,
					(1ULL<<ds),
					d_bw,
					d_bw * (rto_opts.rto_dcols +
						rec_disks[fn]),
					iter_cnt);

				vdev_raidz_map_free(rm_bench);
			}
		}
	}
}

void
run_raidz_benchmark(void)
{
	bench_init_raidz_map();

	run_gen_bench();
	run_rec_bench();

	bench_fini_raidz_maps();
}
