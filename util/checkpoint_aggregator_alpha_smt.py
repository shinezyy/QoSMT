# Copyright (c) 2009 The Regents of The University of Michigan
# Copyright (c) 2011 Advanced Micro Devices, Inc.
# Copyright (c) 2013 Mark D. Hill and David A. Wood
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Lisa Hsu
#          Nilay Vaish

from ConfigParser import ConfigParser
import gzip

import sys, re, os, sh
from os.path import join as pjoin
from os.path import expanduser as pexp
from multiprocessing import Pool
from common import *


solo_cpt_dir = os.environ['st_checkpoint_dir']


class myCP(ConfigParser):
    def __init__(self):
        ConfigParser.__init__(self)

    def optionxform(self, optionstr):
        return optionstr

def aggregate(output_dir, cpts, no_compress, memory_size):
    page_shift = 13
    page_size = 1 << 13

    merged_config = None
    page_ptr = 0

    output_path = output_dir
    if not os.path.isdir(output_path):
        os.system("mkdir -p " + output_path)

    agg_mem_file = open(output_path + "/system.physmem.store0.pmem", "wb+")
    agg_config_file = open(output_path + "/m5.cpt", "wb+")

    if not no_compress:
        merged_mem = gzip.GzipFile(fileobj= agg_mem_file, mode="wb")

    max_curtick = 0
    num_digits = len(str(len(cpts)-1))

    for (i, arg) in enumerate(cpts):
        print arg
        merged_config = myCP()
        config = myCP()
        config.readfp(open(cpts[i] + "/m5.cpt"))

        for sec in config.sections():
            if re.compile("workload").search(sec):
                newsec = re.sub("workload", "workload" + str(i).zfill(num_digits), sec)
                merged_config.add_section(newsec)

                items = config.items(sec)
                for item in items:
                    if item[0] == "ppn":
                        merged_config.set(newsec, item[0], int(item[1]) + page_ptr)
                        continue
                    elif item[0] == "asn":
                        merged_config.set(newsec, item[0],
                                          str(int(item[1]) + i).zfill(num_digits))
                    else:
                        merged_config.set(newsec, item[0], item[1])

                if re.compile("workload.FdMap256$").search(sec):
                    merged_config.set(newsec, "M5_pid", i)

            elif sec == 'system.cpu.xc.0':
                newsec = re.sub("xc\.0", "xc." + str(i).zfill(num_digits), sec)
                merged_config.add_section(newsec)

                items = config.items(sec)
                for item in items:
                    merged_config.set(newsec, item[0], item[1])

            elif sec == "system":
                pass

            elif sec == "system.cpu.isa":
                newsec = re.sub("isa", "isa" + str(i).zfill(num_digits), sec)
                merged_config.add_section(newsec)

                items = config.items(sec)
                for item in items:
                    merged_config.set(newsec, item[0], item[1])

            elif sec == "Globals":
                tick = config.getint(sec, "curTick")
                if tick > max_curtick:
                    max_curtick = tick

            else:
                if i == len(cpts)-1:
                    merged_config.add_section(sec)
                    for item in config.items(sec):
                        merged_config.set(sec, item[0], item[1])

        if i != len(cpts)-1:
            merged_config.write(agg_config_file)

        ### memory stuff
        pages = int(config.get("system", "pagePtr"))
        page_ptr = page_ptr + pages
        print "pages to be read: ", pages

        f = open(cpts[i] + "/system.physmem.store0.pmem", "rb")
        gf = gzip.GzipFile(fileobj=f, mode="rb")

        x = 0
        while x < pages:
            bytesRead = gf.read(page_size)
            if not no_compress:
                merged_mem.write(bytesRead)
            else:
                agg_mem_file.write(bytesRead)
            x += 1

        gf.close()
        f.close()

    merged_config.add_section("system")
    merged_config.set("system", "pagePtr", page_ptr)
    merged_config.set("system", "nextPID", len(cpts))

    '''
    file_size = page_ptr * page_size
    dummy_data = "".zfill(page_size)
    while file_size < memory_size:
        if not no_compress:
            merged_mem.write(dummy_data)
        else:
            agg_mem_file.write(dummy_data)
        file_size += page_size
        page_ptr += 1
    '''

    print "WARNING: "
    print "Make sure the simulation using this checkpoint has at least ",
    print page_ptr, "x 8K of memory"
    merged_config.set("system.physmem.store0", "range_size", page_ptr * page_size)

    merged_config.add_section("Globals")
    merged_config.set("Globals", "curTick", max_curtick)
    merged_config.set("Globals", "numMainEventQueues", 1)

    merged_config.write(agg_config_file)

    if not no_compress:
        merged_mem.close()
        agg_mem_file.close()
    else:
        agg_mem_file.close()


def get_cpt(benchmark_cpt_dir):
    dirs = [d for d in os.listdir(benchmark_cpt_dir) \
            if os.path.isdir(pjoin(benchmark_cpt_dir,d))]

    num_cpts = 0
    for d in dirs:
        if d.startswith('cpt.simpoint'):
            num_cpts += 1

    if num_cpts != 1:
        print 'Number of checkpoint-like directories in {} is {}!'.format(
            benchmark_cpt_dir, num_cpts)
        sys.exit()

    for d in dirs:
        if d.startswith('cpt.simpoint'):
            return pjoin(benchmark_cpt_dir,d)


def aggregator(pair):
    memory_size = '8GB'
    gem5_dir = os.environ['gem5_root']
    benchmarks = get_benchmarks('all_function_spec.txt')
    global solo_cpt_dir
    no_compress = False

    cpts = {}

    for b in benchmarks:
        cpts[b] = get_cpt(pjoin(solo_cpt_dir, b))
        assert(cpts[b])

    print 'Aggregating', pair
    output_dir = pjoin(merged_cpt_dir(), pair[0]+'_'+pair[1])
    if os.path.isdir(output_dir) and os.path.isfile(pjoin(output_dir, 'done')):
        print 'Skip {} because it has been done'.format(pair)
        return
    aggregate(pjoin(merged_cpt_dir(), pair[0]+'_'+pair[1]+'/cpt.0'),
              [cpts[pair[0]], cpts[pair[1]]],
              no_compress,
              memory_size
             )
    sh.touch(pjoin(output_dir, 'done'))


def batch():
    memory_size = '8GB'
    gem5_dir = os.environ['gem5_root']
    benchmarks = get_benchmarks('all_function_spec.txt')
    global solo_cpt_dir
    if not os.path.isdir(merged_cpt_dir()):
        print '{} is not directory!\n'.format(merged_cpt_dir())
        sys.exit()

    cpts = {}

    for b in benchmarks:
        cpts[b] = get_cpt(pjoin(solo_cpt_dir, b))
        assert(cpts[b])

    print 'Scanned following {} checkpoints:'.format(len(cpts))
    print_list(list(cpts))

    user_verify()

    num_threads = 12

    pairs = []
    num_skipped = 0

    for x in benchmarks:
        for y in benchmarks:
            xy = [x, y]
            x_y = pjoin(merged_cpt_dir(), x + '_' + y)

            if not os.path.isdir(x_y):
                os.makedirs(x_y)
            elif has_merged_cpt(x, y):
                num_skipped += 1
                continue
            pairs.append(xy)

    print '{} pairs have been skipped because done'.format(num_skipped)
    print 'Will map following {} pairs to {} workers'.format(len(pairs), num_threads)
    print_list(pairs)
    user_verify()

    print 'Will output to', merged_cpt_dir()
    user_verify()

    p = Pool(num_threads)
    p.map(aggregator, pairs)
    #map(aggregator, pairs)



if __name__ == "__main__":
    from argparse import ArgumentParser
    parser = ArgumentParser("usage: %prog [options] <directory names which "\
                            "hold the checkpoints to be combined>")
    parser.add_argument("-n", "--no-arg", action='store_true',
                       help='If set, no other argument is needed')
    parser.add_argument("-o", "--output-dir", action="store",
                        help="Output directory")
    parser.add_argument("-c", "--no-compress", action="store_true")
    parser.add_argument("--cpts", nargs='+')
    parser.add_argument("--memory-size", action="store", type=int)

    # Assume x86 ISA.  Any other ISAs would need extra stuff in this script
    # to appropriately parse their page tables and understand page sizes.
    options = parser.parse_args()

    if options.no_arg:
        batch()
    else:
        print options.cpts, len(options.cpts)
        if len(options.cpts) <= 1:
            parser.error("You must specify atleast two checkpoint files that "\
                         "need to be combined.")

        aggregate(options.output_dir, options.cpts, options.no_compress,
                  options.memory_size)

