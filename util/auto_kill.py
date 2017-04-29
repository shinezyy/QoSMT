#!/usr/bin/env python2.7

from subprocess import call
from time import sleep
import os
from common import user_verify


my_uid = 1001 # get it by run
# awk -F: '($3 >= 1000) {printf "%s:%s\n",$1,$3}' /etc/passwd

sig = '-TERM'
#sig = '-USR2'

pids = [pid for pid in os.listdir('/proc') if pid.isdigit()]

print 'Will send {} to all gem5 instances:'.format(sig)
user_verify()

for pid in pids:
    try:
        is_my_gem5 = True
        for line in open(os.path.join('/proc', pid, 'status')):
            if line.startswith('Name:'):
                if not line.split()[1].startswith('gem5_fast'):
                    is_my_gem5 = False
            if line.startswith('Uid:'):
                if my_uid != int(line.split()[1]):
                    is_my_gem5 = False

        if is_my_gem5: # and int(pid) > 6800:
            print ["kill", sig, str(pid)]
            call(["kill", sig, str(pid)])

    except IOError:
        continue

