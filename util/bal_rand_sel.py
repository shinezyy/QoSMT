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

while bms4:
    rand1 = pop_rand(bms4)
    rand2 = pop_rand(bms4)
    pairs.append((rand1, rand2))

for pair in pairs:
    print pair[0], pair[1]
