import m5
import os
from m5.objects import *

import os

# These three directory paths are not currently used.
# Add a new environment variable in your bashrc: cpu_2006_dir=<CPU2006 path>
spec_dir = os.environ['cpu_2006_dir']+'/'
my_suffix = '_base.gcc-alpha-4.3'
edu_flag = False


def init_env():
    global edu_flag
    if 'spec_version' in os.environ:
        if os.environ['spec_version'] == 'education':
            edu_flag = True


#400.perlbench
def get_perlbench():
    perlbench = LiveProcess()
    perlbench.executable =  spec_dir+'400.perlbench/exe/perlbench' + my_suffix
    perl_input_ref_dir = spec_dir+'400.perlbench/data/ref/input/'
    perl_input_test_dir = spec_dir+'400.perlbench/data/test/input/'
    perl_input_all_dir = spec_dir+'400.perlbench/data/all/input/'
    # TEST CMDS
    # perlbench.cmd = [perlbench.executable] + ['-I.',\
    #         '-I'+perl_input_all_dir+'lib', perl_input_test_dir+'attrs.pl']
    # REF CMDS
    perlbench.cmd = [perlbench.executable] +\
            ['-I'+perl_input_all_dir+'lib',\
             perl_input_ref_dir+'checkspam.pl',\
             '2500', '5', '25', '11', '150', '1', '1', '1', '1']

    # perlbench.cmd = [perlbench.executable] + ['-I./lib',\
    # 'diffmail.pl', '4', '800', '10', '17', '19', '300']
    # perlbench.cmd = [perlbench.executable] + ['-I./lib',\
    # 'splitmail.pl', '1600', '12', '26', '16', '4500']
    #perlbench.output = out_dir+'perlbench.out'
    return perlbench

def get_bzip2():
    #401.bzip2
    bzip2 = LiveProcess()
    bzip2.executable =  spec_dir+'401.bzip2/exe/bzip2' + my_suffix
    bzip2_input_ref_dir = spec_dir + '401.bzip2/data/ref/input/'
    bzip2_input_test_dir = spec_dir + '401.bzip2/data/test/input/'
    # TEST CMDS
    # bzip2.cmd = [bzip2.executable] + [bzip2_input_test_dir+'dryer.jpg', '5']
    # REF CMDS
    bzip2.cmd = [bzip2.executable] + [bzip2_input_ref_dir+'input.source', '280']
    #bzip2.cmd = [bzip2.executable] + ['chicken.jpg', '30']
    #bzip2.cmd = [bzip2.executable] + ['liberty.jpg', '30']
    #bzip2.cmd = [bzip2.executable] + ['input.program', '280']
    #bzip2.cmd = [bzip2.executable] + ['text.html', '280']
    #bzip2.cmd = [bzip2.executable] + ['input.combined', '200']
    #bzip2.output = out_dir + 'bzip2.out'
    return bzip2

#403.gcc
def get_gcc():
    init_env()
    gcc = LiveProcess()
    gcc.executable = spec_dir+'403.gcc/exe/gcc' + my_suffix
    gcc_input_ref_dir = spec_dir+'403.gcc/data/ref/input/'
    gcc_input_test_dir = spec_dir+'403.gcc/data/test/input/'
    # TEST CMDS
    #gcc.cmd = [gcc.executable] + [gcc_input_test_dir+'cccp.i', '-o', 'cccp.s']
    # REF CMDS
    cmd = [gcc.executable] + [gcc_input_ref_dir+'166.i', '-o', '166.s']
    #gcc.cmd = [gcc.executable] + ['200.i', '-o', '200.s']
    #gcc.cmd = [gcc.executable] + ['c-typeck.i', '-o', 'c-typeck.s']
    #gcc.cmd = [gcc.executable] + ['cp-decl.i', '-o', 'cp-decl.s']
    #gcc.cmd = [gcc.executable] + [gcc_input_ref_dir+'expr.i', '-o', 'expr.s']
    #gcc.cmd = [gcc.executable] + ['expr2.i', '-o', 'expr2.s']
    #gcc.cmd = [gcc.executable] + ['g23.i', '-o', 'g23.s']
    #gcc.cmd = [gcc.executable] + ['s04.i', '-o', 's04.s']
    #gcc.cmd = [gcc.executable] + ['scilab.i', '-o', 'scilab.s']
    #gcc.output = out_dir + 'gcc.out'

    if edu_flag:
        print 'spec edu version'
        cmd[1] = cmd[1]+'n'
    gcc.cmd = cmd

    return gcc

def get_bwaves():
    #410.bwaves
    bwaves = LiveProcess()
    bwaves.executable = spec_dir + '410.bwaves/exe/bwaves' + my_suffix
    bwaves_input_ref_dir = spec_dir + '410.bwaves/data/ref/input/'
    bwaves_input_test_dir = spec_dir + '410.bwaves/data/test/input/'
    # TEST CMDS
    # bwaves.cmd = [bwaves.executable] + [bwaves_input_test_dir+'bwaves.in']
    # REF CMDS
    bwaves.cmd = [bwaves.executable] + [bwaves_input_ref_dir+'bwaves.in']
    #bwaves.output = out_dir + 'bwaves.out'
    return bwaves

#416.gamess
gamess=LiveProcess()
gamess.executable = 'gamess' + my_suffix
# TEST CMDS
#gamess.cmd = [gamess.executable]
#gamess.input = 'exam29.config'
# REF CMDS
gamess.cmd = [gamess.executable]
gamess.input = 'cytosine.2.config'
#gamess.cmd = [gamess.executable]
#gamess.input = 'h2ocu2+.gradient.config'
#gamess.cmd = [gamess.executable]
#gamess.input = 'triazolium.config'
#gamess.output = out_dir + 'gamess.out'

def get_mcf():
    #429.mcf
    mcf = LiveProcess()
    mcf.executable = spec_dir + '429.mcf/exe/mcf' + my_suffix
    mcf_input_ref_dir = spec_dir + '429.mcf/data/ref/input/'
    mcf_input_test_dir = spec_dir + '429.mcf/data/test/input/'
    # TEST CMDS
    # mcf.cmd = [mcf.executable] + [mcf_input_test_dir + 'inp.in']
    # REF CMDS
    mcf.cmd = [mcf.executable] + [mcf_input_ref_dir + 'inp.in']
    #mcf.output = out_dir + 'mcf.out'
    return mcf


def get_leslie3d():
    #437.leslie3d
    leslie3d = LiveProcess()
    leslie3d.executable = spec_dir + '429.leslie3d/exe/leslie3d' + my_suffix
    leslie3d_input_ref_dir = spec_dir + '429.leslie3d/data/ref/input/'
    leslie3d_input_test_dir = spec_dir + '429.leslie3d/data/test/input/'
    # TEST CMDS
    # leslie3d.cmd = [leslie3d.executable] + [leslie3d_input_test_dir + 'leslie3d.in']
    # REF CMDS
    leslie3d.cmd = [leslie3d.executable] + [leslie3d_input_ref_dir + 'leslie3d.in']
    #leslie3d.output = out_dir + 'leslie3d.out'
    return leslie3d

#444.namd
namd = LiveProcess()
namd.executable = 'namd' + my_suffix
# TEST CMDS
# namd.cmd = [namd.executable] + ['--input',\
# 'namd.input', '--output', 'namd.out', '--iterations', '1']
# REF CMDS
namd.cmd = [namd.executable] + ['--input',\
'namd.input', '--output', 'namd.out', '--iterations', '38']
#namd.output = out_dir + 'namd.out'

def get_gobmk():
    #445.gobmk
    gobmk=LiveProcess()
    gobmk.executable = spec_dir + '445.gobmk/exe/gobmk' + my_suffix
    gobmk_input_ref_dir = spec_dir + '445.gobmk/data/ref/input/'
    gobmk_input_test_dir = spec_dir + '445.gobmk/data/test/input/'
    # TEST CMDS
    # gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    # gobmk.input = gobmk_input_test_dir + 'capture.tst'
    # REF CMDS
    gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    gobmk.input = gobmk_input_ref_dir + '13x13.tst'
    #gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    #gobmk.input = 'nngs.tst'
    #gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    #gobmk.input = 'score2.tst'
    #gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    #gobmk.input = 'trevorc.tst'
    #gobmk.cmd = [gobmk.executable] + ['--quiet','--mode', 'gtp']
    #gobmk.input = 'trevord.tst'
    #gobmk.output = out_dir + 'gobmk.out'

    return gobmk

#447.dealII
####### NOT WORKING #########
dealII=LiveProcess()
dealII.executable = 'dealII' + my_suffix
# TEST CMDS
####### NOT WORKING #########
#dealII.cmd = [gobmk.executable]+['8']
# REF CMDS
####### NOT WORKING #########
#dealII.output = out_dir + 'dealII.out'

#450.soplex
soplex=LiveProcess()
soplex.executable = 'soplex' + my_suffix
# TEST CMDS
#soplex.cmd = [soplex.executable] + ['-m10000', 'test.mps']
# REF CMDS
soplex.cmd = [soplex.executable] + ['-m45000', 'pds-50.mps']
#soplex.cmd = [soplex.executable] + ['-m3500', 'ref.mps']
#soplex.output = out_dir + 'soplex.out'

#453.povray
povray=LiveProcess()
povray.executable = 'povray' + my_suffix
# TEST CMDS
#povray.cmd = [povray.executable] + ['SPEC-benchmark-test.ini']
# REF CMDS
povray.cmd = [povray.executable] + ['SPEC-benchmark-ref.ini']
#povray.output = out_dir + 'povray.out'

#454.calculix
calculix=LiveProcess()
calculix.executable = 'calculix' + my_suffix
# TEST CMDS
#calculix.cmd = [calculix.executable] + ['-i', 'beampic']
# REF CMDS
calculix.cmd = [calculix.executable] + ['-i', 'hyperviscoplastic']
#calculix.output = out_dir + 'calculix.out'

def get_hmmer():
    #456.hmmer
    hmmer=LiveProcess()
    hmmer.executable = spec_dir + '456.hmmer/exe/hmmer' + my_suffix
    hmmer_input_test_dir = spec_dir + '456.hmmer/data/test/input/'
    # TEST CMDS
    hmmer.cmd = [hmmer.executable] +\
            ['--fixed', '0', '--mean', '325', '--num',\
             '45000', '--sd', '200', '--seed', '0',\
             hmmer_input_test_dir + 'bombesin.hmm']
    # REF CMDS
    # hmmer.cmd = [hmmer.executable] + ['nph3.hmm', 'swiss41']
    # hmmer.cmd = [hmmer.executable] + ['--fixed', '0', '--mean', '500', '--num',\
            # '500000', '--sd', '350', '--seed', '0', 'retro.hmm']
    #hmmer.output = out_dir + 'hmmer.out'

    return hmmer

def get_sjeng():
    #458.sjeng
    sjeng=LiveProcess()
    sjeng.executable = spec_dir + '458.sjeng/exe/sjeng' + my_suffix
    sjeng_input_test_dir = spec_dir + '458.sjeng/data/test/input/'
    # TEST CMDS
    sjeng.cmd = [sjeng.executable] + [sjeng_input_test_dir+'test.txt']
    # REF CMDS
    #sjeng.cmd = [sjeng.executable] + ['ref.txt']
    #sjeng.output = out_dir + 'sjeng.out'

    return sjeng

#459.GemsFDTD
GemsFDTD=LiveProcess()
GemsFDTD.executable = 'GemsFDTD' + my_suffix
# TEST CMDS
#GemsFDTD.cmd = [GemsFDTD.executable]
# REF CMDS
GemsFDTD.cmd = [GemsFDTD.executable]
#GemsFDTD.output = out_dir + 'GemsFDTD.out'

def get_libquantum():
    #462.libquantum
    libquantum=LiveProcess()
    libquantum.executable = spec_dir + '462.libquantum/exe/libquantum' + my_suffix
    # TEST CMDS
    # libquantum.cmd = [libquantum.executable] + ['33','5']
    # REF CMDS [UPDATE 10/2/2015]: Sparsh Mittal has pointed out the correct input
    # for libquantum should be 1397 and 8, not 1297 and 8. Thanks!
    libquantum.cmd = [libquantum.executable] + ['1397','8']
    #libquantum.output = out_dir + 'libquantum.out'
    return libquantum

def get_h264ref():
    #464.h264ref
    h264ref=LiveProcess()
    h264ref.executable = spec_dir + '464.h264ref/exe/h264ref' + my_suffix
    test_dir = spec_dir + '464.h264ref/data/test/input/'
    ref_dir = spec_dir + '464.h264ref/data/ref/input/'
    # TEST CMDS
    #h264ref.cmd = [h264ref.executable] + ['-d',\
    #        test_dir + 'foreman_test_encoder_baseline.cfg']
    # REF CMDS
    #h264ref.cmd = [h264ref.executable] + ['-d', 'foreman_ref_encoder_baseline.cfg']
    h264ref.cmd = [h264ref.executable] + ['-d',\
              ref_dir + 'foreman_ref_encoder_main.cfg']
    #h264ref.cmd = [h264ref.executable] + ['-d', 'sss_encoder_main.cfg']
    #h264ref.output = out_dir + 'h264ref.out'
    return h264ref

#465.tonto
tonto=LiveProcess()
tonto.executable = 'tonto' + my_suffix
# TEST CMDS
#tonto.cmd = [tonto.executable]
# REF CMDS
tonto.cmd = [tonto.executable]
#tonto.output = out_dir + 'tonto.out'

#470.lbm
lbm=LiveProcess()
lbm.executable = 'lbm' + my_suffix
# TEST CMDS
# lbm.cmd = [lbm.executable] + ['20', 'reference.dat', '0', '1',\
# '100_100_130_cf_a.of']
# REF CMDS
lbm.cmd = [lbm.executable] + ['300', 'reference.dat', '0', '0',\
'100_100_130_ldc.of']
#lbm.output = out_dir + 'lbm.out'

def get_omnetpp():
    #471.omnetpp
    omnetpp=LiveProcess()
    omnetpp.executable = spec_dir + '471.omnetpp/exe/omnetpp' + my_suffix
    test_dir = spec_dir + '471.omnetpp/data/test/input/'
    ref_dir = spec_dir + '471.omnetpp/data/ref/input/'
    # TEST CMDS
    # omnetpp.cmd = [omnetpp.executable] + [test_dir+'omnetpp.ini']
    # REF CMDS
    omnetpp.cmd = [omnetpp.executable] + [ref_dir + 'omnetpp.ini']
    #omnetpp.output = out_dir + 'omnetpp.out'
    return omnetpp

def get_astar():
    #473.astar
    astar=LiveProcess()
    astar.executable = spec_dir + '473.astar/exe/astar' + my_suffix
    test_dir = spec_dir + '473.astar/data/test/input/'
    ref_dir = spec_dir + '473.astar/data/ref/input/'
    # TEST CMDS
    # astar.cmd = [astar.executable] + [test_dir + 'lake.cfg']
    # REF CMDS
    astar.cmd = [astar.executable] + [ref_dir + 'rivers.cfg']
    #astar.output = out_dir + 'astar.out'
    return astar

#481.wrf
wrf=LiveProcess()
wrf.executable = 'wrf' + my_suffix
# TEST CMDS
#wrf.cmd = [wrf.executable]
# REF CMDS
wrf.cmd = [wrf.executable]
#wrf.output = out_dir + 'wrf.out'

#482.sphinx3
sphinx3=LiveProcess()
sphinx3.executable = 'sphinx_livepretend' + my_suffix
# TEST CMDS
#sphinx3.cmd = [sphinx3.executable] + ['ctlfile', '.', 'args.an4']
# REF CMDS
sphinx3.cmd = [sphinx3.executable] + ['ctlfile', '.', 'args.an4']
#sphinx3.output = out_dir + 'sphinx3.out'

#483.xalancbmk
######## NOT WORKING ###########
xalancbmk=LiveProcess()
xalancbmk.executable = 'xalancbmk' + my_suffix
# TEST CMDS
######## NOT WORKING ###########
#xalancbmk.cmd = [xalancbmk.executable] + ['-v','test.xml','xalanc.xsl']
# REF CMDS
######## NOT WORKING ###########
#xalancbmk.output = out_dir + 'xalancbmk.out'

def get_specrand_i():
    #999.specrand
    specrand_i=LiveProcess()
    specrand_i.executable = spec_dir + '999.specrand/exe/specrand' + my_suffix
    # TEST CMDS
    # specrand_i.cmd = [specrand_i.executable] + ['324342', '24239']
    # REF CMDS
    specrand_i.cmd = [specrand_i.executable] + ['1255432124', '234923']
    #specrand_i.output = out_dir + 'specrand_i.out'
    return specrand_i

#998.specrand
specrand_f=LiveProcess()
specrand_f.executable = 'specrand' + my_suffix
# TEST CMDS
#specrand_f.cmd = [specrand_f.executable] + ['324342', '24239']
# REF CMDS
specrand_f.cmd = [specrand_f.executable] + ['1255432124', '234923']
#specrand_f.output = out_dir + 'specrand_f.out'
