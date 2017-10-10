#!/usr/bin/env python2.7

import os
from os.path import join as pjoin
from common import *
import random


random.seed(os.urandom(8))

def pop_rand(x):
    i = random.randrange(0, len(x))
    return x.pop(i)


file_dir = os.path.dirname(os.path.abspath(__file__))
bms = get_benchmarks(pjoin(file_dir, 'all_function_spec.txt'))
bms4 = 4*bms

pairs = []

for bm in bms:
    neighbors = []
    i = 0
    while i < 4:
        rand = pop_rand(bms4)
        if rand not in neighbors:
            pairs.append((bm, rand))
            neighbors.append(rand)
            i += 1
        else:
            bms4.append(rand)

for pair in pairs:
    print pair[0], pair[1]
