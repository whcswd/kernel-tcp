import math
import random
import pathlib
from liebes.CiObjects import *
import numpy as np
from liebes.analysis import CIAnalysis
from liebes.EHelper import EHelper
from liebes.sql_helper import SQLHelper
from liebes.ci_logger import logger
from liebes.tcp_approach import topic_model
from liebes.tcp_approach.information_achieve import TestCaseInformationManager
from liebes.tcp_approach.metric_manager import DistanceMetric
from liebes.tcp_approach.topic_model import TopicModel
from liebes import GitHelper
from liebes.tokenizer import AstTokenizer
import os
import multiprocessing as mp


def ARP(distance_arr, test_cases):
    k = 10
    candidate_list = []
    prioritized_list = []
    test_cases_len = len(test_cases)
    idx_list = list(range(0, test_cases_len))
    first_idx = random.choice(idx_list)
    prioritized_list.append(first_idx)
    idx_list.remove(first_idx)
    idx_list_len = len(idx_list)
    while idx_list_len != 0:
        candidate_list = []
        k = min(k, idx_list_len)
        candidate_list = random.sample(idx_list, k)
        d = [[0] * len(candidate_list) for _ in range(len(prioritized_list))]
        col = 0
        for candidate_idx in candidate_list:
            row = 0
            for p_idx in prioritized_list:
                distance = distance_arr[p_idx][candidate_idx]
                d[row][col] = distance
                row += 1
            col += 1

        neighbours = list(np.min(d, axis=0))
        max_idx = neighbours.index(max(neighbours))
        next_pt_idx = candidate_list[max_idx]

        prioritized_list.append(next_pt_idx)
        idx_list.remove(next_pt_idx)
        # print(idx_list_len)
        idx_list_len -= 1

    return prioritized_list

def do_exp(ltp_version):
    ehelper = EHelper()

    history_infos = ehelper.get_checkout_info()

    # information_manager = TestCaseInformationManager(cia, git_helper=GitHelper("/home/userpath/ltp"))
    tokenizer = AstTokenizer()
    distance_manager = DistanceMetric()
    ehelper = EHelper()
    apfd_arr = []
    tokens = {}


    for hi_idx in range(len(history_infos)):
        hi = history_infos[hi_idx]
        git_sha = hi[0]
        all_cases = []
        faults_arr = []
        # ltp_version = ehelper.get_ltp_version(git_sha)
        tc_distance_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
        ltp_path = f'/home/userpath/projects/ltp_projects/ltp_{ltp_version}/ltp-{ltp_version}'
        selftest_path = f'/home/userpath/projects/kernel-tcp'

        for fault in hi[2]:
            # if r'selftests' in fault:
            #     continue
            for idx in range(len(hi[1])):
                if fault == hi[1][idx]:
                    faults_arr.append(idx)
        
        if len(faults_arr) == 0:
            continue
        
        for tc in hi[1]:
            if 'test_cases/selftests' not in tc:
                all_cases.append(f'{ltp_path}/{tc[16: -1]}')
            else:
                all_cases.append(f'{selftest_path}/{tc[1: -1]}')

        documents = []
        topic_model = TopicModel()
        for tc in all_cases:
            t_type = TestCaseType.C
            if tc.endswith(r'.sh'):
                t_type = TestCaseType.SH
            try:
                token = tokens.get(tc)
                if token is None:
                    text = Path(tc).read_text(encoding='utf-8', errors='ignore')
                    token = tokenizer.get_tokens(text, t_type)
                    tokens[tc] = token
                documents.append(" ".join(token))
            except Exception:
                tokens[tc] = " "
                documents.append(" ")
        topic_model.train(documents)
        topic_value = {}
        for tc in all_cases:
            token = tokens.get(tc)
            tv = topic_model.get_topic(" ".join(token))
            temp = [0] * topic_model.number_of_topics
            for t in tv:
                temp[t[0]] = t[1]
            topic_value[tc] = temp
        distance_arr = np.zeros((len(all_cases), len(all_cases)))
        for i in range(len(all_cases)):
            for j in range(i + 1, len(all_cases)):
                distance = distance_manager.jensen_shannon_distance(
                    topic_value[all_cases[i]],
                    topic_value[all_cases[j]]
                )
                distance_arr[i][j] = distance_arr[j][i] = distance
        # fill the array
        # print(distance_arr)
        # process of ARP
        distance_arr = np.array(distance_arr)
    
        choose_one = random.randint(0, len(all_cases) - 1)
    
        prioritized_list = ARP(distance_arr, all_cases)
    
        apfd = ehelper.APFD(faults_arr, prioritized_list)
    
        logger.info(f"topic_model distance_metric: jensen_shannon_distance;  commit: {git_sha}, apfd: {apfd}, ltp_version: {ltp_version}")
        apfd_arr.append(apfd)


    print(f"avg apfd: {np.mean(apfd_arr)}")


if __name__ == '__main__':
    ltp_version = [
        '20230127',
        '20230516',
        '20230929',
        '20240129'
    ]

    pool = mp.Pool()
    pool.map(do_exp, ltp_version)
    pool.close()
    pool.join()

    # do_exp(None)