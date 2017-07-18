#!/usr/bin/env python2.7

import os
import re
import sys
import random
import sh
import time
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool
from argparse import ArgumentParser

from common import *

opt = None


def get_pairs(inf):
    x = []
    with open(inf) as f:
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
    if not opt.debug:
        gem5_m_time = os.path.getmtime(pjoin(os.environ['gem5_build'], 'gem5.fast'))
    else:
        gem5_m_time = os.path.getmtime(pjoin(os.environ['gem5_build'], 'gem5.opt'))
    ret = []

    for pair in pairs:
        if not opt.single_thread:
            pair_dir = pair[0] + '_' + pair[1]
        else:
            pair_dir = pair[0]
        benchmark_dir = pjoin(uexp(opt.output_dir), pair_dir)
        time_stamp_file = pjoin(uexp(benchmark_dir), 'done')
        if os.path.isdir(benchmark_dir) and os.path.isfile(time_stamp_file):
            file_m_time = os.path.getmtime(time_stamp_file)
            if file_m_time >= gem5_m_time:
                print 'Time stamp of {} in {} is ' \
                        'newer than gem5 binary, skip!'.format(
                            pair, opt.output_dir)
                continue
        ret.append(pair)
    return ret


def smt_run(pair):
    global opt

    gem5_dir = os.environ['gem5_root']

    if not opt.single_thread:
        pair_dir = pair[0] + '_' + pair[1]
    else:
        pair_dir = pair[0]

    merged_cpt_dir_ = pjoin(merged_cpt_dir(), pair[0] + '_' + pair[1])
    outdir = pjoin(uexp(opt.output_dir), pair_dir)

    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    exec_dir = os.environ['gem5_run_dir']
    os.chdir(exec_dir)

    options = [
        '--outdir=' + outdir,
        pjoin(gem5_dir, 'configs/spec/' + opt.command),
        '--smt',
        '-r', 1,
        '--checkpoint-dir', merged_cpt_dir_,
        '--mem-size=8GB',
        '--benchmark={};{}'.format(pair[0], pair[1]),
        '--benchmark_stdout=' + outdir,
        '--benchmark_stderr=' + outdir,
        '--cpu-type=detailed',
        # '--caches',
        # '--cacheline_size=64',
        # '--l1i_size=64kB',
        # '--l1d_size=64kB',
        # '--l1i_assoc=16',
        # '--l1d_assoc=16',
        # '--l2cache',
        # '--l2_size=4MB',
        # '--l2_assoc=16'
    ]

    if opt.debug_flags:
        options = ['--debug-flags=' + opt.debug_flags] + options
    if opt.debug_start:
        options = ['--debug-start=' + opt.debug_start] + options
    if opt.little:
        options = options + ['--o3cpu-little-core']
    if opt.dup_cache:
        options = options + ['--dup-cache']

    if opt.gem5_args:
        arg_list = str(opt.gem5_args).split('&')
        options = arg_list + options

    if opt.cmd_args:
        arg_list = str(opt.cmd_args).split('&')
        options = options + arg_list

    print options

    # user_verify()
    # sys.exit()

    if not opt.debug:
        sh.gem5_fast(
            _out=pjoin(outdir, 'gem5_out.txt'),
            _err=pjoin(outdir, 'gem5_err.txt'),
            *options
        )
    elif not opt.gdb:
        sh.gem5_opt(
            _out=pjoin(outdir, 'gem5_out.txt'),
            _err=pjoin(outdir, 'gem5_err.txt'),
            *options
        )
    else:
        util_dir = pjoin(os.environ['gem5_root'], 'util')
        os.chdir(util_dir)
        with open('debug.sh', 'w') as f:
            print >>f, 'gdb --args \\'
            print >>f, pjoin(os.environ['gem5_build'], 'gem5.opt'), '\\'
            for line in options:
                print >>f, line, '\\'
        sh.chmod('+x', 'debug.sh')
        return

    # sh.touch(pjoin(outdir, 'done'))


def set_conf(opt):

    gem5_dir = os.environ['gem5_root']
    assert(os.path.isfile(pjoin(gem5_dir, 'configs/spec/' + opt.command)))

    if opt.debug_flags:
        assert opt.debug
    assert opt.single_thread == (opt.command == 'sim_st.py')
    print 'Use script: {}, output to {},' \
            ' {} workers, sim st: {}'.format(
                opt.command, opt.output_dir, opt.thread_number,
                opt.single_thread)

if __name__ == '__main__':
    parser = ArgumentParser(usage='specify output directory and number of threads')
    parser.add_argument('-j', '--thread-number', action='store', required=True,
                        type=int,
                        help='Number of threads of gem5 instance'
                       )

    parser.add_argument('-c', '--command', action='store', required=True,
                        help='gem5 script to use'
                       )

    parser.add_argument('-o', '--output-dir', action='store', required=True,
                        help='gem5 output directory'
                       )

    parser.add_argument('-i', '--input', action='store', required=True,
                        help='Specify benchmark pairs'
                       )

    parser.add_argument('-s', '--single_thread', action='store_true',
                        help='use st config'
                       )

    parser.add_argument('-d', '--debug', action='store_true',
                        help='use opt version'
                       )

    parser.add_argument('--debug-flags', action='store',
                        help='debug flags'
                       )

    parser.add_argument('--debug-start', action='store',
                        help='debug start tick'
                       )

    parser.add_argument('--gdb', action='store_true',
                        help='use gdb to debug'
                       )

    parser.add_argument('-l', '--little', action='store_true',
                        help='little core'
                       )

    parser.add_argument('--dup-cache', action='store_true',
                        help='use duplicated cache per thread'
                       )

    parser.add_argument('--gem5-args', action='store',
                        help='options to gem5 directly'
                       )
    parser.add_argument('--cmd-args', action='store',
                        help='options to command script'
                       )

    opt = parser.parse_args()
    set_conf(opt)
    num_thread = opt.thread_number

    targets = get_pairs(opt.input)
    targets = cpt_filter(targets)
    targets = time_stamp_filter(targets)


    print 'Following {} pairs will be run'.format(len(targets))
    print_list(targets)

    user_verify()

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(smt_run, targets)
    else:
        smt_run(targets[0])

