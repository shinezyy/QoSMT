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

from checkpoint_aggregator_alpha_smt import user_verify

num_thread = 18


def get_spec():
    x = []
    print 'This script should not be used again, check please'
    with open('./simpointed_spec.txt') as f:
        for line in f:
            if not line.startswith('#'):
                x.append(line.strip('\n'))
    return x


def simpoint_bak(benchmark):
    gem5_dir = os.environ['gem5_root']
    all_checkpoing_dir = pjoin(gem5_dir, 'checkpoint')
    all_simpoint_dir = pjoin(gem5_dir, 'simpoint')

    benchmark_dir = pjoin(all_checkpoing_dir, benchmark)
    simpoint_dir = pjoin(all_simpoint_dir, benchmark)

    backup_dir = pjoin(uexp('~/simpoint_bak'), benchmark)

    print 'from {} to {}'.format(simpoint_dir, backup_dir)

    sh.cp('-r', simpoint_dir, backup_dir)



if __name__ == '__main__':
    global num_thread
    num_thread = 6
    bms = get_spec()
    print len(bms), 'benchmarks:', bms, 'will be backup'
    user_verify()
    p = Pool(num_thread)
    p.map(simpoint_bak, bms)
