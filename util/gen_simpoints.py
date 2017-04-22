#!/usr/bin/env python2.7

import os
import random
import sh
import operator
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool


def get_spec():
    x = []
    print 'This script should not be used again, check please'
    assert(0)
    with open('./simpointed_spec.txt') as f:
        for line in f:
            if line.startswith('#'):
                x.append(line.lstrip('#').strip('\n'))
    return x


def get_weights(benchmark):
    print benchmark
    gem5_dir = os.environ['gem5_root']
    simpoints_all = pjoin(gem5_dir, 'simpoint')
    benchmark_dir = pjoin(simpoints_all, benchmark)
    os.chdir(benchmark_dir)
    max_list = []
    for i in range(10): # 10 times of clustering is performed
        weights = 'weights' + str(i)
        simpoints = 'simpoints' + str(i)

        weights_dict = {}
        cluster_dict = {}

        with open(simpoints) as sf:
            for line in sf:
                vector, n = map(int, line.split())
                cluster_dict[n] = vector

        with open(weights) as wf:
            for line in wf:
                weight, n = map(float, line.split())
                n = int(n)
                weights_dict[cluster_dict[n]] = weight

        sort_by_weights = sorted(weights_dict.iteritems(), key=operator.itemgetter(1),
                                 reverse=True)
        max_list.append(sort_by_weights[0])

    max_list = sorted(max_list, key=operator.itemgetter(1), reverse=True)
    print max_list

    max_of_max = max_list[0]

    with open('simpoints', 'w') as sf:
        print >>sf, max_of_max[0], 0
    with open('weights', 'w') as wf:
        print >>wf, 1, 0



def get_simpoint(benchmark):
    gem5_dir = os.environ['gem5_root']
    simpoints_all = pjoin(gem5_dir, 'simpoint')
    simpoint_file_name = 'simpoint.bb.gz'
    random.seed(os.urandom(8))

    benchmark_dir = pjoin(simpoints_all, benchmark)
    os.chdir(benchmark_dir)
    print benchmark_dir

    assert(os.path.isfile(simpoint_file_name))
    assert(os.path.isfile(pjoin(benchmark_dir, simpoint_file_name)))

    for i in range(10):
        weights = 'weights' + str(i)
        simpoints = 'simpoints' + str(i)
        # 10 times of clustering is performed
        sh.simpoint('-loadFVFile', simpoint_file_name,
                    '-saveSimpoints', simpoints,
                    '-saveSimpointWeights', weights,
                    '-inputVectorsGzipped',
                    '-maxK', 30,
                    '-numInitSeeds', 40,
                    '-iters', 100000,
                    '-seedkm', random.randint(0, 2**32 - 1),
                    '-seedproj', random.randint(0, 2**32 - 1),
                    # '-seedsample', random.randint(0, 2**32 - 1),
                   )


if __name__ == '__main__':
    #p = Pool(24)
    #p.map(get_simpoint, get_spec())
    map(get_weights, get_spec())

