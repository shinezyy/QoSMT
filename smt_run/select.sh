../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;sjeng" -o ~/fc/perlbench_sjeng -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;bzip2" -o ~/fc/gcc_bzip2 -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;libquantum" -o ~/fc/libquantum_libquantum -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;libquantum" -o ~/fc/gobmk_libquantum -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;bzip2" -o ~/fc/perlbench_bzip2 -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;astar" -o ~/fc/libquantum_astar -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/fc/bzip2_libquantum -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;gobmk" -o ~/fc/hmmer_gobmk -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "mcf\;astar" -o ~/fc/mcf_astar -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;libquantum" -o ~/fc/gcc_libquantum -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;astar" -o ~/fc/hmmer_astar -s --smt -v fast -a ALPHA_FC
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;sjeng" -o ~/fc/gobmk_sjeng -s --smt -v fast -a ALPHA_FC
