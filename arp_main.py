import math
import random
import pathlib
from liebes.CiObjects import *
import numpy as np
from liebes.analysis import CIAnalysis
from liebes.EHelper import EHelper
from liebes.sql_helper import SQLHelper
from liebes.ci_logger import logger
from liebes.tcp_approach import adaptive_random_prior
import os

if __name__ == '__main__':

    distance_metrics = [
        'hanming_distance'
        # 'edit_distance',
        # 'euclidean_string_distance',
        # 'manhattan_string_distance',
        # 'normalized_compression_distance'
    ]

    k = 10

    candidate_strategies = [
        'min_max'
    ]

    context_strategy = "default"
    # tokenizer = AstTokenizer()
    # ir_model = TfIdfModel()
    # TODO 加个多线程的方式
    summary = []
    # for distance_metric in distance_metrics:
    #     for candidate_strategy in candidate_strategies:
    #         res = adaptive_random_prior.do_exp2(cia, k, distance_metric, candidate_strategy)
    #         summary.append(res)
    # logger.info("summary :---------------------------------")
    # for s in summary:
    #     logger.info(s)

    lines = []
    with open(r'logs/test_info_without_log.txt', r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if r'checkout' in cur_line:
            commit_match = re.search(r'checkout: (.*?)\n', cur_line)
            git_sha = commit_match.group(1)
            next_line = lines[idx + 1]
            total_match = re.search(r'total_cases: \[(.*?)\]', next_line)
            total_cases = total_match.group(1).split(', ')
            next_2_line = lines[idx + 2]
            fault_match = re.search(r'fail_cases: \[(.*?)\]', next_2_line)
            fault_list = fault_match.group(1).split(', ')
            history_infos.append((git_sha, total_cases, fault_list))

    unique_set_without_log = set()
    for hi in history_infos:
        fault_list = hi[2]
        for f in fault_list:
            unique_set_without_log.add(f)
    
    lines = []
    with open(r'logs/test_info.txt', r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if r'checkout' in cur_line:
            commit_match = re.search(r'checkout: (.*?)\n', cur_line)
            git_sha = commit_match.group(1)
            next_line = lines[idx + 1]
            total_match = re.search(r'total_cases: \[(.*?)\]', next_line)
            total_cases = total_match.group(1).split(', ')
            next_2_line = lines[idx + 2]
            fault_match = re.search(r'fail_cases: \[(.*?)\]', next_2_line)
            fault_list = fault_match.group(1).split(', ')
            history_infos.append((git_sha, total_cases, fault_list))

    unique_set = set()
    for hi in history_infos:
        fault_list = hi[2]
        for f in fault_list:
            unique_set.add(f)
    
    print(unique_set_without_log - unique_set)