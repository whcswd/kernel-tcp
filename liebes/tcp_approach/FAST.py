from datasketch import MinHash, MinHashLSH, WeightedMinHash, MinHashLSHForest
import math
import sys
import random
import pathlib
import os
from liebes.CiObjects import *
import json
import numpy as np
from liebes.analysis import CIAnalysis
from liebes.EHelper import EHelper
from liebes.sql_helper import SQLHelper
from liebes.ci_logger import logger
from liebes.tokenizer import AstTokenizer
import traceback
import zlib
import statistics
import matplotlib.pyplot as plt

tokens = {}
tokenizer = AstTokenizer()


def MHSignature(h: int, text: list[str]):
    mh = MinHash(num_perm = h)
    for word in text:
        mh.update(word)
    return mh

def Select(Mv: MinHash, candidate_set: set[str], signature_map: dict[str, MinHash]):
    max_distance = -1
    max_candidate = None
    for candidate in candidate_set:
        signature = signature_map[candidate]
        distance = Mv.jaccard(signature)
        if distance > max_distance:
            max_distance = distance
            max_candidate = candidate
    
    return max_candidate

def FAST(test_cases: list[str], bands: int, rows: int, h: int, threshold: float):

    if bands * rows != h:
        print(f'please set right bands and rows!')
        exit(-1)

    lsh = MinHashLSH(threshold = threshold, num_perm = h, params = (bands, rows))
    mh_map = {}
    idx_map = {}

    for idx in range(len(test_cases)):
        test_case = test_cases[idx]
        idx_map[test_case] = idx
        t_type = TestCaseType.C
        if test_case.endswith(r'.sh'):
            t_type = TestCaseType.SH
        
        token = tokens.get(test_case)
        if token is None:
            if os.path.exists(test_case):
                text = Path(test_case).read_text(encoding='utf-8', errors='ignore')
            else:
                text = ''
            # text = Path(test_case).read_text(encoding='utf-8', errors='ignore')
            token_tmp = tokenizer.get_tokens(text, t_type)
            token = [t.encode('utf-8') for t in token_tmp]
            tokens[test_case] = token
        
        mh = MHSignature(h, token)
        lsh.insert(test_case, mh)
        mh_map[test_case] = mh

    prioritized_list = []
    selected_set = set()
    size = len(test_cases)
    Mv = MHSignature(h, [''.encode('utf-8')])

    while len(prioritized_list) != size:
        Cs = lsh.query(Mv)
        if len(Cs) == 0:
            Mv = MHSignature(h, [''.encode('utf-8')])
            Cs = lsh.query(Mv)
        
        candidate_set = set(test_cases) - selected_set - set(Cs)
        if len(candidate_set) == 0:
            candidate_set = set(test_cases) - selected_set
        candidate = Select(Mv, candidate_set, mh_map)
        if candidate is None:
            pass
        prioritized_list.append(idx_map[candidate])
        lsh.remove(candidate)
        selected_set.add(candidate)
        Mv = MinHash.union(Mv, mh_map[candidate])
    
    return prioritized_list


def do_exp(log_path: str, bands: int, rows: int, h: int, threshold: float, ltp_version: str):
    ehelper = EHelper()
    apfd_res = []
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = ehelper.get_checkout_info()
    
    for hi in history_infos:
        git_sha = hi[0]
        test_cases = []
        faults_arr = []

        for fault in hi[2]:
            for idx in range(len(hi[1])):
                if fault == hi[1][idx]:
                    faults_arr.append(idx)
        
        if len(faults_arr) == 0:
            continue

        # ltp_version = ehelper.get_ltp_version(git_sha)
        tc_distance_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
        ltp_path = f'/home/userpath/projects/ltp_projects/ltp_{ltp_version}/ltp-{ltp_version}'
        selftest_path = f'/home/userpath/projects/kernel-tcp'

        # for tc in hi[1]:
        #     if 'test_cases/selftests' not in tc:
        #         test_cases.append(f'{ltp_path}/{tc[16: -1]}')
        #     else:
        #         test_cases.append(tc[1: -1])
        
        for tc in hi[1]:
            if 'test_cases/selftests' not in tc:
                test_cases.append(f'{ltp_path}/{tc[16: -1]}')
            else:
                test_cases.append(f'{selftest_path}/{tc[1: -1]}')
        try:
            prioritized_list = FAST(test_cases, bands, rows, h, threshold)
            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.debug(f"bands: {bands}, rows: {rows}, h: {h}, commit: {git_sha}, apfd: {apfd_v}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"bands: {bands}, rows: {rows}, h: {h}, commit: {git_sha}, avg apfd: {np.average(apfd_res)}")
    return f"bands: {bands}, rows: {rows}, h: {h}, commit: {git_sha}, avg apfd: {np.average(apfd_res)}"
        

