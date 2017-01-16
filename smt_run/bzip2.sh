../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;perlbench" -o ~/dynamic/bzip2_perl -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;bzip2" -o ~/dynamic/bzip2_bzip2 -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;gcc" -o ~/dynamic/bzip2_gcc -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;mcf" -o ~/dynamic/bzip2_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;gobmk" -o ~/dynamic/bzip2_gobmk -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;hmmer" -o ~/dynamic/bzip2_hmmer -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;sjeng" -o ~/dynamic/bzip2_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/dynamic/bzip2_libq -s --smt -v fast -a ALPHA_DYN
