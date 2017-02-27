../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;perlbench" -o ~/dyn_2tcm/hmmer_perl -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;mcf" -o ~/dyn_2tcm/hmmer_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;sjeng" -o ~/dyn_2tcm/hmmer_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;mcf" -o ~/dyn_2tcm/perl_mcf -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;bzip2" -o ~/dyn_2tcm/bzip2_bzip2 -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/dyn_2tcm/bzip2_libq -s --smt -v fast -a ALPHA_DYN
