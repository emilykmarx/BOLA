#!/usr/bin/env python2
# -*- coding: utf-8 -*-
"""
Created on Wed May 27 14:26:21 2020

@author: emily
"""
from collections import defaultdict
import numpy as np
import pprint

# For size and SSIM:
# Influx query groups by {channel, format}; average over channel to get a single ladder of len 10

def parse(filename):   
    with open(filename) as f:
        # channel => [ 10 sizes ]
        ladders = defaultdict(lambda: []) 
        cur_channel = ""
        # Get ladder from each channel
        for line in f:
            fields = line.split()
            if len(fields) == 0:
                continue
            if fields[0] == "tags:":
                # Channel line
                cur_channel = fields[1]
            elif fields[0][0] == "1":
                # Size line
                val = float(fields[1])
                ladders[cur_channel].append(val)
        # Sort ladders increasing
        for channel,ladder in ladders.items():
            ladder.sort()
       
        # Average ladders across channels    
        return np.mean(ladders.values(), axis=0)
    
# Print conveniently
print('Avg size (bytes):')
avg_sizes = parse('avg_sizes')
pprint.pprint([int(round(size)) for size in avg_sizes])

print('Avg ssim_index:')
avg_ssims = parse('avg_ssims')
pprint.pprint(avg_ssims)