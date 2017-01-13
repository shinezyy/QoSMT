../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;sjeng" -o ~/smt_fix/bzip2_sjeng -s --smt -v opt -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;mcf" -o ~/smt_fix/libquantum_mcf -s --smt -v opt -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;gobmk" -o ~/smt_fix/gcc_gobmk -s --smt -v opt -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "mcf\;gcc" -o ~/smt_fix/mcf_gcc -s --smt -v opt -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;bzip2" -o ~/smt_fix/gobmk_bzip2 -s --smt -v opt -a ALPHA_DYN
