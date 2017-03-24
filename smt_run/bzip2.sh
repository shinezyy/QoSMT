../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;perlbench" -o ~/dyn_bpp2/bzip2_perl -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;bzip2" -o ~/dyn_bpp2/bzip2_bzip2 -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;gcc" -o ~/dyn_bpp2/bzip2_gcc -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;mcf" -o ~/dyn_bpp2/bzip2_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;gobmk" -o ~/dyn_bpp2/bzip2_gobmk -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;hmmer" -o ~/dyn_bpp2/bzip2_hmmer -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;sjeng" -o ~/dyn_bpp2/bzip2_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/dyn_bpp2/bzip2_libq -s --smt -v fast -a ALPHA_DYN
