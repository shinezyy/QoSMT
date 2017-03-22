../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;bzip2" -o ~/dyn_0321/bzip2_bzip2 -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "bzip2\;sjeng" -o ~/dyn_0321/bzip2_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "sjeng\;sjeng" -o ~/dyn_0321/sjeng_sjeng -s --smt -v fast -a ALPHA_DYN
../run_gem5_alpha_spec06_benchmark.sh -b "sjeng\;bzip2" -o ~/dyn_0321/sjeng_bzip2 -s --smt -v fast -a ALPHA_DYN
