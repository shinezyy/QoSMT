../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;sjeng" -o ~/dyn_test/perlbench_sjeng -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;bzip2" -o ~/dyn_test/gcc_bzip2 -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;libquantum" -o ~/dyn_test/libquantum_libquantum -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;libquantum" -o ~/dyn_test/gobmk_libquantum -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "perlbench\;bzip2" -o ~/dyn_test/perlbench_bzip2 -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "libquantum\;astar" -o ~/dyn_test/libquantum_astar -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;libquantum" -o ~/dyn_test/bzip2_libquantum -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;gobmk" -o ~/dyn_test/hmmer_gobmk -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "mcf\;astar" -o ~/dyn_test/mcf_astar -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "gcc\;libquantum" -o ~/dyn_test/gcc_libquantum -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "hmmer\;astar" -o ~/dyn_test/hmmer_astar -s --smt -v fast -a ALPHA_CC
../run_gem5_alpha_spec06_benchmark.sh -b "gobmk\;sjeng" -o ~/dyn_test/gobmk_sjeng -s --smt -v fast -a ALPHA_CC
