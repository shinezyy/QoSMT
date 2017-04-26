import sys
import os
from os.path import join as pjoin


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


def merged_cpt_dir():
    return pjoin(os.environ['gem5_root'], 'checkpoint_merge')


def has_merged_cpt(x, y):
    mcd = pjoin(merged_cpt_dir(), x + '_' + y)
    if not os.path.isdir(mcd):
        return False
    elif os.path.isfile(pjoin(mcd, 'done')):
        return True
    else:
        return False


def print_list(l):
    cur_line_len = 0
    for x in l:
        if cur_line_len + len(str(x)) > 80:
            print ''
            cur_line_len = 0
        print x,
        cur_line_len += len(str(x))
    print ''


def print_option(opt):
    cur_line_len = 0
    for line in opt:
        if line.startswith('-') or cur_line_len + len(line) > 80:
            print ''
            cur_line_len = 0
        cur_line_len += len(line)
        print line,
    print ''
