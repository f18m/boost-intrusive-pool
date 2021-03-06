#!/usr/bin/python
"""Generates a figure that shows all benchmarking results
"""
import sys
import os
import json
import collections

import matplotlib.pyplot as plotlib

BenchmarkPoint = collections.namedtuple('BenchmarkPoint', ['cpu_time', 'enlarge_step'], verbose=False)
filled_markers = ('o', 'v', '^', '<', '>', '8', 's', 'p', '*', 'h', 'H', 'D', 'd', 'P', 'X')
colours = ('r', 'g', 'b', 'black', 'yellow', 'purple')

def plot_graphs(outfilename, benchmark_dict, title):
    """Plots the given dictionary of benchmark results
    """
    plotlib.clf()
    plotlib.xlabel('Memory pool enlarge step')
    plotlib.ylabel('Average CPU time per allocation')
    plotlib.title(title)

    nmarker=0
    max_x=[]
    max_y=[]
    for impl_name in benchmark_dict.keys():
        current_bm = benchmark_dict[impl_name]
        
        # add a line plot
        X = [ x.enlarge_step for x in current_bm ]
        Y = [ y.cpu_time for y in current_bm ]
        #lines = plotlib.plot(X, Y, '-' + filled_markers[nmarker], label=impl_name)
        #plotlib.setp(lines, 'color', colours[nmarker])

        # add a semilogy plot        
        lines = plotlib.semilogx(X, Y, '-' + filled_markers[nmarker], label=impl_name)
        plotlib.setp(lines, 'color', colours[nmarker])
        plotlib.grid(True)
        
        # remember max X/Y
        max_x.append(max(X))
        max_y.append(max(Y))
        
        nmarker=nmarker+1

    # set some graph global props:        
    #plotlib.xlim(0, max(max_x)*1.1)
    plotlib.ylim(0, max(max_y)*1.3)

    print("Writing plot into '%s'" % outfilename)
    plotlib.legend(loc='upper left')
    plotlib.savefig(outfilename)
    plotlib.show()

def load_pattern(pattern):
    bm = {}
    bm['boost_intrusive_pool'] = []
    bm['plain_malloc'] = []
    num_items = []
    
    num_runs = len(pattern)-1 # one entry is the pattern type
    print(" ...pattern %s: found %d runs" % (type, num_runs))
    for run_idx in range(1,num_runs+1):
        run = pattern["run_" + str(run_idx)]
        num_items.append(run["num_items"])
        
        # each different line must be a different key in the dictionary
        # each key in the dict can have multiple data points:
        bm['boost_intrusive_pool'].append(BenchmarkPoint(run['boost_intrusive_pool']['duration_nsec_per_item'], run['enlarge_step']))
        bm['plain_malloc'].append(BenchmarkPoint(run['plain_malloc']['duration_nsec_per_item'], run['enlarge_step']))
        
    #print(' ...found {} data points in implementation {}...'.format(len(bm[pattern_name]), pattern_name))
    return [ bm, int(min(num_items)), int(max(num_items)) ]

def main(args):
    """Program Entry Point
    """
    if len(args) != 2:
        print('Usage: %s <image-output-file> <json-file-with-results> ...' % sys.argv[0])
        sys.exit(os.EX_USAGE)

    bm = {}
    image_output_tag = args[0]
    filepath = args[1]

    print("Parsing '{}'...".format(filepath))
    with open(filepath, 'r') as benchfile:
        filename = os.path.basename(filepath)
        
        try:
            pattern_list = json.load(benchfile)
        except Exception as ex:
            print("Invalid JSON file {}: {}".format(filepath, ex))
            sys.exit(2)
        
        #print json.dumps(pattern_list, sort_keys=True, indent=4, separators=(',', ': '))
        print("Found %d patterns" % len(pattern_list))
        
        for pattern_idx in range(1,len(pattern_list)+1):
            pattern_name = "pattern_" + str(pattern_idx)
            pattern = pattern_list[pattern_name]
            desc = pattern["desc"]
            bm, min_items, max_items = load_pattern(pattern)
            
            outfilename = "tests/results/" + pattern_name + "_" + image_output_tag + ".png"
            title = image_output_tag + "\n" + desc + "\nNumber of items allocated: "
            if min_items == max_items:
                title += str(min_items)
            else:
                title += "[" + str(min_items) + "-" + str(max_items) + "]"
            
            plot_graphs(outfilename, bm, title)

if __name__ == '__main__':
    main(sys.argv[1:])


