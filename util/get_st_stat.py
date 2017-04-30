#!/usr/bin/env python2.7

import os
import re
import sys
import random
import sh
import operator
import psutil
import time
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool

from common import user_verify


def get_spec():
    x = []
    with open('./all_function_spec.txt') as f:
    #with open('./test_bm.txt') as f:
        for line in f:
            if not line.startswith('#'):
                x.append(line.lstrip('#').strip('\n'))
    return x


def smt_run(pair):
    memory_size = '4GB'
    gem5_dir = os.environ['gem5_root']
    pair_dir = pair[0] + '_' + pair[1]
    merged_cpt_dir = pjoin(gem5_dir, 'checkpoint_merge/' + pair_dir)
    outdir = pjoin(uexp('~/sim_st_0430'), pair[0])
    if not os.path.isdir(outdir):
        os.makedirs(outdir)

    exec_dir = pjoin(gem5_dir, 'smt_run')
    os.chdir(exec_dir)

    options = (
        '--outdir=' + outdir,
        #'--debug-flags=LB',
        pjoin(gem5_dir, 'configs/spec/sim_st.py'),
        '--smt',
        '-r', 1,
        '--checkpoint-dir', merged_cpt_dir,
        '--mem-size=4GB',
        '--benchmark={};{}'.format(pair[0], pair[1]),
        '--benchmark_stdout=' + outdir,
        '--benchmark_stderr=' + outdir,
        '--cpu-type=detailed',
        '--caches',
        '--cacheline_size=64',
        '--l1i_size=64kB',
        '--l1d_size=64kB',
        '--l1i_assoc=16',
        '--l1d_assoc=16',
        '--l2cache',
        '--l2_size=4MB',
        '--l2_assoc=16'
    )

    print options

    # user_verify()

    # sys.exit()

    sh.gem5_fast(
        _out=pjoin(outdir, 'gem5_out.txt'),
        _err=pjoin(outdir, 'gem5_err.txt'),
        *options
    )


if __name__ == '__main__':
    num_thread  = 12
    targets = get_spec()
    pairs = [[x, 'gcc'] for x in targets]
    print 'Following {} pairs will be run'.format(len(targets)), pairs
    user_verify()

    p = Pool(num_thread)
    p.map(smt_run, pairs)
    #map(smt_run, pairs)

