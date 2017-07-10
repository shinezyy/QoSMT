
def proc(line, spliter):
    return line.split(spliter)

old_d_tick = 0
old_s_tick = 0
old_inst = ''

with open('/home/zyy/st/bwaves/gem5_out.txt') as st, \
        open('/home/zyy/part/bwaves_mcf/gem5_out.txt') as dyn:
    count = -1
    for d, s in zip(dyn, st):
        count += 1
        if count > 21:
            d_tick, d_inst = proc(d, ': system.cpu.commit: ')
            s_tick, s_inst = proc(s, ': system.cpu.commit: ')
            d_tick = int(d_tick)
            s_tick = int(s_tick)
            d_inst = d_inst.strip()
            s_inst = s_inst.strip()
            '''
            if (d_inst != s_inst):
                print('not the same @', count)
                print(d, '  <=>  ', s)
            '''
            if (d_tick < s_tick):
                print('Exceed @',count)
                break

            st_time = s_tick - old_s_tick
            smt_time = d_tick - old_d_tick
            if smt_time + 5000 < st_time:
                print('SMT faster @ {}, \n {} ======> {}'.format(count, old_inst, d_inst))
                print('ST time: {}, SMT time: {}'.format(st_time, smt_time))

            old_d_tick = d_tick
            old_s_tick = s_tick
            old_inst = d_inst

