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


output_dir = '~/dyn_0425'


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


def time_stamp_filter(pairs):
    global output_dir
    gem5_m_time = os.path.getmtime(pjoin(os.environ['gem5_build'], 'gem5.fast'))
    ret = []

    for pair in pairs:
        pair_dir = pair[0] + '_' + pair[1]
        benchmark_dir = pjoin(uexp(output_dir), pair_dir)
        time_stamp_file = pjoin(uexp(benchmark_dir), 'done')
        if os.path.isdir(benchmark_dir) and os.path.isfile(time_stamp_file):
            file_m_time = os.path.getmtime(time_stamp_file)
            if file_m_time >= gem5_m_time:
                print 'Time stamp of {} in {} is ' \
                        'newer than gem5 binary, skip!'.format(pair, output_dir)
                continue
        ret.append(pair)
    return ret


def smt_run(pair):
    gem5_dir = os.environ['gem5_root']
    pair_dir = pair[0] + '_' + pair[1]
    merged_cpt_dir_ = pjoin(merged_cpt_dir(), pair_dir)
    global output_dir
    outdir = pjoin(uexp(output_dir), pair_dir)

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
        '--mem-size=8GB',
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

    sh.touch(pjoin(outdir, 'done'))


if __name__ == '__main__':
    num_thread  = 10

    targets = get_pairs()
    targets = cpt_filter(targets)
    targets = time_stamp_filter(targets)


    print 'Following {} pairs will be run'.format(len(targets))
    print_list(targets)
    user_verify()

    p = Pool(num_thread)
    p.map(smt_run, targets)
    #map(smt_run, targets)

