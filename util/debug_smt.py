#!/usr/bin/env python2.7

import os
import sys
import sh
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool
from argparse import ArgumentParser

from common import *


def smt_run(pair, use_fast, checkpoint_tick, resume_checkpoint):
    memory_size = '4GB'
    gem5_dir = os.environ['gem5_root']
    pair_dir = pair[0] + '_' + pair[1]
    merged_cpt_dir_ = pjoin(merged_cpt_dir(), pair_dir)
    outdir = pjoin(uexp('~/debug_gem5'), pair_dir)
    if not os.path.isdir(outdir):
        os.makedirs(outdir)

    stat_file = pjoin(outdir, 'stats.txt')
    if use_fast:
        gem5_bin = pjoin(os.environ['gem5_build'], 'gem5.fast')
    else:
        gem5_bin = pjoin(os.environ['gem5_build'], 'gem5.opt')

    if os.path.isfile(stat_file) and left_is_older(gem5_bin, stat_file):
        print 'Gem5 is older than stats.txt!!!'
        user_verify()

    exec_dir = pjoin(gem5_dir, 'smt_run')
    os.chdir(exec_dir)

    options = [
        '--outdir=' + outdir,
        #'--debug-flags=LB',
        pjoin(gem5_dir, 'configs/spec/dyn.py'),
        '--smt',
        '-r', '1',
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
        '--l2_assoc=16',
    ]

    if checkpoint_tick:
        options.append('--take-checkpoints={},1000000'.format(checkpoint_tick))
        options.append('--max-checkpoints=1')
    elif resume_checkpoint:
        pass
    else:
        options.append('--checkpoint-dir={}'.format(merged_cpt_dir_))

    print_option(options)
    user_verify()
    # sys.exit()

    if use_fast:
        sh.gem5_fast(
            _out=pjoin(outdir, 'gem5_out.txt'),
            _err=pjoin(outdir, 'gem5_err.txt'),
            *options
        )
    else:
        sh.gem5_opt(
            _out=pjoin(outdir, 'gem5_out.txt'),
            _err=pjoin(outdir, 'gem5_err.txt'),
            *options
        )


if __name__ == '__main__':
    parser = ArgumentParser(usage='specify gem5 version and benchmark pair')
    parser.add_argument('-f', '--fast', action='store_true',
                       help='If set, use gem5.fast; default gem5.opt') # opt default
    parser.add_argument('-p', '--pair', action='store',
                       help='pairs of benchmark, split by ,')
    parser.add_argument('-c', '--checkpoint-tick', action='store',
                       help='tick to take checkpoint')
    parser.add_argument('-r', '--resume-checkpoint', action='store_true',
                       help='resume from checkpoint from outdir')

    opt = parser.parse_args()

    x, y = str(opt.pair).split(',')

    if not has_merged_cpt(x, y):
        print 'merged cpt of {} {} NOT found !'.format(x, y)
        sys.exit()

    print 'Pair of {} {} will be run'.format(x, y)

    smt_run((x, y), opt.fast, opt.checkpoint_tick, opt.resume_checkpoint)

