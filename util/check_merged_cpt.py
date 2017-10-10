import os
from common import *


with open('./selected_pairs2.txt') as inf:
    for line in inf:
        pair = line.rstrip().replace(' ', '_')
        done_path = pjoin('/home/share/checkpoint_merge', pair, 'done')
        if not os.path.isfile(done_path):
            print done_path, 'is not file'
