#!/bin/bash
#
# run_gem5_alpha_spec06_benchmark.sh
# Author: Mark Gottscho Email: mgottscho@ucla.edu
# Copyright (C) 2014 Mark Gottscho
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

############ DIRECTORY VARIABLES: MODIFY ACCORDINGLY #############
# Install location of gem5
GEM5_DIR=$(dirname $0)
##################################################################

# Arg Parser!

# Get command line input. We will need to check these.
while echo $1 | grep -q ^-; do
    eval $( echo $1 | sed 's/^-//' )=$2
    shift
    shift
done

echo benchmark = $b                   # Benchmark name, e.g. bzip2
benchmark=$b
echo output_dir = $o                   # Directory to place run output.
output_dir=$o
echo smt = $s
smt=$s
echo debug_flags = $d
debug_flags=$d
echo gem5_ver = $v
gem5_ver=$v
echo arch = $a
arch=$a

SCRIPT_OUT=$output_dir/runscript.log
# File log for this script's stdout henceforth

################## REPORT SCRIPT CONFIGURATION ###################

echo "Command line:"                                | tee $SCRIPT_OUT
echo "$0 $*"                                        | tee -a $SCRIPT_OUT
echo "================= Hardcoded directories ==================" | tee -a $SCRIPT_OUT
echo "GEM5_DIR:                                     $GEM5_DIR" | tee -a $SCRIPT_OUT
echo "==================== Script inputs =======================" | tee -a $SCRIPT_OUT
echo "benchmark:                                    $benchmark" | tee -a $SCRIPT_OUT
echo "output_dir:                                   $output_dir" | tee -a $SCRIPT_OUT
echo "==========================================================" | tee -a $SCRIPT_OUT
##################################################################


#################### LAUNCH GEM5 SIMULATION ######################
echo ""

echo "" | tee -a $SCRIPT_OUT
echo "" | tee -a $SCRIPT_OUT
echo "--------- Here goes nothing! Starting gem5! ------------" | tee -a $SCRIPT_OUT
echo "" | tee -a $SCRIPT_OUT
echo "" | tee -a $SCRIPT_OUT

################# detailed
#gdb --args \
nohup \
$GEM5_DIR/build/$arch/gem5.$gem5_ver\
    --debug-flags="O3PipeView,SI"\
    --debug-start=1000000\
    --debug-file=trace.out\
    --outdir=$output_dir\
    $GEM5_DIR/configs/spec/spec06_config.py\
    --benchmark="$benchmark"\
    --benchmark_stdout=$output_dir\
    --benchmark_stderr=$output_dir\
    --mem-size=4GB\
    --cpu-type=detailed\
    --caches\
    --cacheline_size=64\
    --l1i_size=64kB\
    --l1d_size=64kB\
    --l1i_assoc=16\
    --l1d_assoc=16\
    --l2cache\
    --l2_size=4MB\
    --l2_assoc=16\
    $smt\
    > nohup.$benchmark 2>&1 &
