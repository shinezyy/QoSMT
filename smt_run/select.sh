../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;mcf" -o ~/dyn_0317/perl_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;bzip2" -o ~/dyn_0317/bzip2_bzip2 -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;gcc" -o ~/dyn_0317/bzip2_gcc -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/dyn_0317/bzip2_libq -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;gcc" -o ~/dyn_0317/gcc_gcc -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;perlbench" -o ~/dyn_0317/gcc_perl -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;gobmk" -o ~/dyn_0317/gcc_gobmk -s --smt -v fast -a ALPHA_DYN
