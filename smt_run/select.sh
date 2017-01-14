../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;sjeng" -o ~/dynamic/bzip2_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;mcf" -o ~/dynamic/libquantum_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;gobmk" -o ~/dynamic/gcc_gobmk -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "mcf\;gcc" -o ~/dynamic/mcf_gcc -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;bzip2" -o ~/dynamic/gobmk_bzip2 -s --smt -v fast -a ALPHA_DYN
