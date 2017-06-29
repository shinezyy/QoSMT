with open('./dyn_smt_bug.txt') as dyn, open('./sim_st_debug.txt') as st:
    for d, s in zip(dyn, st):
        if d != s:
            print(d, '  <=>  ', s)
