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

from common import *


def get_pairs():
    x = []
    with open('./selected_pairs') as f:
        for line in f:
            a, b = line.strip('\n').split()
            x.append([a, b])
    return x


def cpt_filter(pairs):
    ret = []
    for pair in pairs:
        # merged checkpoint directory
        mcd = pjoin(merged_cpt_dir(), pair[0] + '_' + pair[1])
        if os.path.isfile(pjoin(mcd, 'done')):
            ret.append(pair)
        else:
            print pair, 'has no merged cpt, skip!'
    return ret


def smt_run(pair):
    memory_size = '4GB'
    gem5_dir = os.environ['gem5_root']
    pair_dir = pair[0] + '_' + pair[1]
    merged_cpt_dir_ = pjoin(merged_cpt_dir(), pair_dir)
    outdir = pjoin(uexp('~/dyn_0425'), pair_dir)
    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    exec_dir = pjoin(gem5_dir, 'smt_run')
    os.chdir(exec_dir)

    options = (
        '--outdir=' + outdir,
        #'--debug-flags=LB',
        pjoin(gem5_dir, 'configs/spec/dyn.py'),
        '--smt',
        '-r', 1,
        '--checkpoint-dir', merged_cpt_dir_,
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

    sh.gem5_exe(
        _out=pjoin(outdir, 'gem5_out.txt'),
        _err=pjoin(outdir, 'gem5_err.txt'),
        *options
    )


if __name__ == '__main__':
    global num_thread
    num_thread  = 10
    all_targets = get_pairs()
    targets = cpt_filter(all_targets)

    print 'Following {} pairs will be run'.format(len(targets))
    print_list(targets)
    user_verify()

    p = Pool(num_thread)
    p.map(smt_run, targets)
    #map(smt_run, targets)

