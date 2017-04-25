import sys


def get_benchmarks(bm_file):
    ret = []
    with open(bm_file) as f:
        for line in f:
            ret.append(line.strip('\n'))
    return ret


def user_verify():
    ok = raw_input('Is that OK? (y/n)')
    if ok != 'y':
        sys.exit()

