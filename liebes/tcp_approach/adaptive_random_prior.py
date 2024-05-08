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
from liebes.tcp_approach import metric_manager
from liebes.ci_logger import logger
import Levenshtein
import traceback
import zlib
import statistics
import matplotlib.pyplot as plt

ehelper = EHelper()
distance_global_map = {}

def normalized_compression_distance(sequence_a, sequence_b):
    sequence_a = sequence_a.encode("utf8")
    sequence_b = sequence_b.encode("utf8")
    concatenated_sequence = sequence_a + sequence_b
    compressed_concatenated = zlib.compress(concatenated_sequence)
    compressed_a = zlib.compress(sequence_a)
    compressed_b = zlib.compress(sequence_b)

    ncd = (len(compressed_concatenated) - min(len(compressed_a), len(compressed_b))) / max(len(compressed_a),
                                                                                            len(compressed_b))
    return ncd

def euclidean_string_distance(s1, s2):
    len_diff = abs(len(s1) - len(s2))
    if len(s1) < len(s2):
        s1 += '\0' * len_diff
    elif len(s1) > len(s2):
        s2 += '\0' * len_diff
    point1 = [ord(char) for char in s1]
    point2 = [ord(char) for char in s2]
    squared_diff = sum((p1 - p2) ** 2 for p1, p2 in zip(point1, point2))
    return math.sqrt(squared_diff)

def jaccard_distance_function(set1, set2):
    set1 = set(set1)
    set2 = set(set2)
    intersection = len(set1.intersection(set2))
    union = len(set1.union(set2))

    distance = 1 - intersection / union
    return distance

def manhattan_string_distance(s1, s2):
    len_diff = abs(len(s1) - len(s2))
    if len(s1) < len(s2):
        s1 += '\0' * len_diff
    elif len(s1) > len(s2):
        s2 += '\0' * len_diff
    point1 = [ord(char) for char in s1]
    point2 = [ord(char) for char in s2]

    distance = 0
    for i in range(len(point1)):
        distance += abs(point1[i] - point2[i])
    return distance

def edit_distance(s1, s2):
    # m, n = len(s1), len(s2)
    # dp = [[0] * (n + 1) for _ in range(m + 1)]

    # for i in range(m + 1):
    #     dp[i][0] = i
    # for j in range(n + 1):
    #     dp[0][j] = j

    # for i in range(1, m + 1):
    #     for j in range(1, n + 1):
    #         if s1[i - 1] == s2[j - 1]:
    #             dp[i][j] = dp[i - 1][j - 1]
    #         else:
    #             dp[i][j] = min(dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + 1)

    # return dp[m][n]

    return Levenshtein.distance(s1, s2)

def hanming_distance(s1, s2):
    len_diff = abs(len(s1) - len(s2))
    if len(s1) < len(s2):
        s1 += '\0' * len_diff
    elif len(s1) > len(s2):
        s2 += '\0' * len_diff
    distance = 0
    for i in range(len(s1)):
        if s1[i] != s2[i]:
            distance += 1
    return distance

# def get_distance_map(test_cases: list[Test], distance_metric: str):
#     dm = DistanceMetric()
#     map_file = r'distance_' + distance_metric + r'_map.json'
#     distance_map = {}
#     for t1 in test_cases:
#         for t2 in test_cases:
#             t1_k = t1.file_path
#             t2_k = t2.file_path
#             distance = distance_map.get(t2_k, {}).get(t1_k, None)
#             if distance is None:
#                 t1_text = Path(t1_k).read_text(encoding='utf-8', errors='ignore')
#                 t2_text = Path(t2_k).read_text(encoding='utf-8', errors='ignore')
#                 if distance_metric == 'hanming_distance':
#                     distance = hanming_distance(t1_text, t2_text)
#                 if distance_metric == 'edit_distance':
#                     distance = edit_distance(t1_text, t2_text)
#                 elif distance_metric == 'euclidean_string_distance':
#                     distance = euclidean_string_distance(t1_text, t2_text)
#                 elif distance_metric == 'manhattan_string_distance':
#                     distance = manhattan_string_distance(t1_text, t2_text)
#                 elif distance_metric == 'normalized_compression_distance':
#                     distance = dm.normalized_compression_distance(t1_text, t2_text)
#                 tmp_dict = distance_map.get(t1_k, {})
#                 tmp_dict[t2_k] = distance
#                 distance_map[t1_k] = tmp_dict

#             else: 
#                 tmp_dict = distance_map.get(t1_k, {})
#                 tmp_dict[t2_k] = distance
#                 distance_map[t1_k] = tmp_dict
        
#     with open(map_file, r'w') as f:
#         json.dump([distance_map], f, indent=4)
#     f.close()
    
#     return distance_map

def get_distance_map(test_cases: list[str], distance_metric: str, map_path: str):
    dm = DistanceMetric()
    map_file = f'{map_path}/distance_{distance_metric}_map.json'
    print(map_file)
    distance_map = {}
    for t1_k in test_cases:
        if not os.path.exists(t1_k):
            continue
        for t2_k in test_cases:
            if not os.path.exists(t2_k):
                continue
            distance = distance_map.get(t2_k, {}).get(t1_k, None)
            if distance is None:
                t1_text = Path(t1_k).read_text(encoding='utf-8', errors='ignore')
                t2_text = Path(t2_k).read_text(encoding='utf-8', errors='ignore')
                if distance_metric == 'hanming_distance':
                    distance = hanming_distance(t1_text, t2_text)
                if distance_metric == 'edit_distance':
                    distance = edit_distance(t1_text, t2_text)
                elif distance_metric == 'euclidean_string_distance':
                    distance = euclidean_string_distance(t1_text, t2_text)
                elif distance_metric == 'manhattan_string_distance':
                    distance = manhattan_string_distance(t1_text, t2_text)
                elif distance_metric == 'normalized_compression_distance':
                    distance = dm.normalized_compression_distance(t1_text, t2_text)
                tmp_dict = distance_map.get(t1_k, {})
                tmp_dict[t2_k] = distance
                distance_map[t1_k] = tmp_dict

            else: 
                tmp_dict = distance_map.get(t1_k, {})
                tmp_dict[t2_k] = distance
                distance_map[t1_k] = tmp_dict
        
    with open(map_file, r'w') as f:
        json.dump([distance_map], f, indent=4)
    f.close()
    
    return distance_map

    

from liebes.tcp_approach.metric_manager import DistanceMetric
distance_map = {}

def ARP(test_cases: list[str], k: int, distance_metric: str, candidate_stragety: str):

    dm = DistanceMetric()

    if distance_metric is None:
        distance_metric = 'edit_distance'
    if candidate_stragety is None:
        candidate_stragety = 'min_max'

    # distance_global_map = get_distance_map(test_cases, distance_metric)
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
            candidate = test_cases[candidate_idx]
            row = 0
            for p_idx in prioritized_list:
                pt = test_cases[p_idx]
                distance = distance_global_map.get(pt, {}).get(candidate, None)
                if distance is None:
                    distance = distance_global_map.get(candidate, {}).get(pt, None)
                if distance is None:
                    distance = 10000
                d[row][col] = distance
                row += 1
            col += 1

        if candidate_stragety == 'min_max':
            neighbours = list(np.min(d, axis=0))
            max_idx = neighbours.index(max(neighbours))
            next_pt_idx = candidate_list[max_idx]

        prioritized_list.append(next_pt_idx)
        idx_list.remove(next_pt_idx)
        # print(idx_list_len)
        idx_list_len -= 1

    return prioritized_list

def load_map(distance_metric: str, ltp_version: str):
    global distance_global_map
    map_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
    map_file = f'{map_path}/distance_{distance_metric}_map.json'
    with open(map_file, r'r') as f:
        distance_global_map = json.load(f)[0]
    f.close()


def history_drive(distance_metric: str, candidate_stragety: str, version_gap: int, history_path: str):
    ehelper = EHelper()
    lines = []
    with open(history_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if distance_metric in cur_line and r'commit' in cur_line:
            commit_match = re.search(r'commit: (.*?),', cur_line)
            apfd_match = re.search(r'apfd: (.*?)\n', cur_line)
            git_sha = commit_match.group(1)
            apfd = apfd_match.group(1)
            next_line = lines[idx + 1]
            prioritized_match = re.search(r'prioritized_list: \[(.*?)\]', next_line)
            prioritized_list = prioritized_match.group(1).split(', ')
            next_2_line = lines[idx + 2]
            fault_match = re.search(r'faults_arr: \[(.*?)\]', next_2_line)
            fault_list = fault_match.group(1).split(', ')
            history_infos.append((git_sha, prioritized_list, fault_list, apfd))
    
    history_infos.reverse()
    coun = 0
    apfd_v = 0.0
    for idx in range(len(history_infos)):
        history_idx = idx + version_gap
        if history_idx >= len(history_infos):
            continue
        cur_version = history_infos[idx]
        history_version = history_infos[history_idx]
        cur_faults = cur_version[2]
        history_prioritized_list = history_version[1]
        is_exsit = True
        for fault in cur_faults:
            if fault not in history_prioritized_list:
                is_exsit = False
                break
        if not is_exsit:
            continue
        
        history_prioritized_idx_list = [i for i in range(len(history_prioritized_list))]
        cur_faults_idx = []
        for i in range(len(history_prioritized_list)):
            if history_prioritized_list[i] in cur_faults:
                cur_faults_idx.append(i)
        
        apfd = ehelper.APFD(cur_faults_idx, history_prioritized_idx_list)
        apfd_v += apfd
        coun += 1
        logger.info(f"distance_metric: {distance_metric}, commit: {cur_version[0]}, version_gap: {version_gap}, apfd: {apfd}, cur_apfd: {cur_version[3]}, history_apfd: {history_version[3]}")
        # history_faults = history_version[2]
        # jaccard_distance = 1 - len(set(cur_faults).intersection(set(history_faults))) / len(set(cur_faults).union(set(history_faults)))
        # logger.info(f'jaccard distance: {jaccard_distance}')
    
    apfd_v = apfd_v / coun
    logger.info(f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_v)}")
        
def failed_case_distance_analysis(distance_metric: str, candidate_stragety: str, history_path: str):
    lines = []
    with open(history_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if distance_metric in cur_line and r'commit' in cur_line:
            commit_match = re.search(r'commit: (.*?),', cur_line)
            apfd_match = re.search(r'apfd: (.*?)\n', cur_line)
            git_sha = commit_match.group(1)
            apfd = apfd_match.group(1)
            next_2_line = lines[idx + 2]
            fault_match = re.search(r'faults_arr: \[(.*?)\]', next_2_line)
            fault_list = fault_match.group(1).split(', ')
            history_infos.append((git_sha, fault_list, apfd))
    
    distance_map = get_distance_map([], distance_metric)
    visited_set = set()
    distance_list = []
    file_size_list = []
    for t1 in distance_map:
        file_size_list.append(len(Path(t1).read_text(encoding='utf-8', errors='ignore')))
    #     t1_map = distance_map.get(t1)
    #     for t2 in t1_map:
    #         if (t2, t1) in visited_set:
    #             continue
    #         distance_list.append(t1_map.get(t2))
    #         visited_set.add((t1, t2))
    # distance_list = sorted(distance_list)
    
    file_size_list = sorted(file_size_list)

    x = [i for i in range(len(file_size_list))]
    

    mean_avg_idx = 0.0
    median_avg_idx = 0.0
    coun = 0
    visited_set = set()
    important_points = []
    fault_size_list = []
    fault_frequency = {}
    fault_size_map = {}
    suite_apfg_map = {'kself': 0.0, 'ltp': 0.0}
    ltp_coun = 0
    for hi in history_infos:
        fault_list = hi[1]
        if 'ltp' in fault_list[0]:
            suite_apfg_map['ltp'] = suite_apfg_map['ltp'] + float(hi[2])
            ltp_coun += 1
        else:
            suite_apfg_map['kself'] = suite_apfg_map['kself'] + float(hi[2])
        
        for fault in fault_list:
            # fault_distance_list = []
            # fault_distance_map = distance_map.get(fault[1: -1])
            # for k in fault_distance_map:
            #     fault_distance_list.append(fault_distance_map[k])

            # median = statistics.median(fault_distance_list)
            # mean = statistics.mean(fault_distance_list)

            # for idx in range(len(distance_list)):
            #     if distance_list[idx] > mean:
            #         mean_idx = idx
            #         break
            
            # for idx in range(len(distance_list)):
            #     if distance_list[idx] > median:
            #         median_idx = idx
            #         break
            
            fault_frequency[fault] = fault_frequency.get(fault, 0) + 1
            if fault not in visited_set:
                fault_file_size = len(Path(fault[1: -1]).read_text(encoding='utf-8', errors='ignore'))
                fault_size_list.append(fault_file_size)
                size_idx = 0
                for idx in range(len(file_size_list)):
                    if fault_file_size < file_size_list[idx]:
                        size_idx = idx
                        break
                
                fault_size_map[fault] = size_idx
                # mean_avg_idx += mean_idx
                # median_avg_idx += median_idx
                # coun += 1
                # logger.info(f"distance_metric: {distance_metric}, fault: {fault}, mean_idx: {mean_idx}/{len(distance_list)}, median_idx: {median_idx}/{len(distance_list)}")
                logger.info(f"distance_metric: {distance_metric}, fault: {fault}, size_idx: {size_idx}/{len(file_size_list)}")
                visited_set.add(fault)
                # important_points.append((fault, mean_idx))
                important_points.append((fault, size_idx))
    
    # important_values = [distance_list[ip[1]] for ip in important_points]
    # plt.plot(x, distance_list)
    # plt.scatter([ip[1] for ip in important_points], important_values, color='red')
    # plt.legend()
    # for i in range(len(important_points)):
    #     plt.annotate(f'{important_points[i][0]}, {important_values[i]}', xy=(important_points[i][1], important_values[i]))
    # plt.savefig('tmp.jpg')
                
    # important_values = [file_size_list[ip[1]] for ip in important_points]
    # plt.plot(x, file_size_list)
    # plt.scatter([ip[1] for ip in important_points], important_values, color='red')
    # plt.legend()
    # plt.savefig('tmp.jpg')

    print(fault_frequency)
    x = [k[k.rfind('/'): -1] for k in fault_frequency.keys()]
    plt.bar(x, list(fault_frequency.values()))
    plt.savefig('tmp.jpg')

    suite_apfg_map['ltp'] = suite_apfg_map['ltp'] / ltp_coun
    suite_apfg_map['kself'] = suite_apfg_map['kself'] / (len(history_infos) - ltp_coun)
    print(f'ltp_count: {ltp_coun},  detail: {suite_apfg_map}')

    for hi in history_infos:
        faults_idxs = []
        for f in hi[1]:
            faults_idxs.append(fault_size_map[f])
        logger.info(f"distance_metric: {distance_metric}, faults: {hi[1]}, size_idxs: {faults_idxs}, apfd: {hi[2]}")

    # logger.info(f'distance_metric: {distance_metric}, mean_avg_idx: {mean_avg_idx/coun}, median_avg_idx: {median_avg_idx/coun}')


def do_exp(cia: CIAnalysis, k: int, distance_metric: str, candidate_stragety: str):
    ehelper = EHelper()
    apfd_res = []
    for ci_index in range(1, len(cia.ci_objs)):
        ci_obj = cia.ci_objs[ci_index]
        # start iteration of one test result
        # 1. first extract the faults result
        test_cases = ci_obj.get_all_testcases()
        faults_arr = []
        for i in range(len(test_cases)):
            if test_cases[i].is_failed():
                faults_arr.append(i)
        if len(faults_arr) == 0:
            continue

        try:
            prioritized_list = ARP(test_cases, k, distance_metric, candidate_stragety)
            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.debug(f"distance_metric: {distance_metric}, commit: {ci_obj.instance.git_sha}, apfd: {apfd_v}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}")
    return f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}"

def do_exp2(log_path: str, k: int, distance_metric: str, candidate_stragety: str):
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = ehelper.get_checkout_info()
    
    apfd_res = []
    for hi_idx in range(len(history_infos)):
        hi = history_infos[hi_idx]
        git_sha = hi[0]
        test_cases = []
        faults_arr = []

        ltp_version = ehelper.get_ltp_version(git_sha)
        tc_distance_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
        ltp_path = f'/home/userpath/projects/ltp_projects/ltp_{ltp_version}/ltp-{ltp_version}'
        load_map(distance_metric, ltp_version)

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
                test_cases.append(f'{ltp_path}/{tc[16: -1]}')
            else:
                test_cases.append(tc[1: -1])

        try:
            prioritized_list = ARP(test_cases, k, distance_metric, candidate_stragety)

            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.info(f"distance_metric: {distance_metric}, commit: {git_sha}, apfd: {apfd_v}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}")
    return f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}"

def do_exp3(log_path: str, distance_metric: str, ltp_version: str):
    tc_distance_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
    ltp_path = f'/home/userpath/projects/ltp_projects/ltp_{ltp_version}/ltp-{ltp_version}'
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = ehelper.get_checkout_info()
    
    tests_set = set()
    for hi_idx in range(len(history_infos)):
        hi = history_infos[hi_idx]
        git_sha = hi[0]
        test_cases = []
        faults_arr = []

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
                tests_set.add(f'{ltp_path}/{tc[16: -1]}')
            else:
                tests_set.add(tc[1: -1])
    
    try:
        get_distance_map(list(tests_set), distance_metric, tc_distance_path)
            # pass
    except:
        traceback.print_exc()

def do_exp4(log_path: str, k: int, distance_metric: str, candidate_stragety: str, ltp_version: str):
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = ehelper.get_checkout_info()
    
    apfd_res = []
    for hi_idx in range(len(history_infos)):
        hi = history_infos[hi_idx]
        git_sha = hi[0]
        test_cases = []
        faults_arr = []

        tc_distance_path = f'/home/userpath/projects/testcase_distance/ltp_{ltp_version}'
        ltp_path = f'/home/userpath/projects/ltp_projects/ltp_{ltp_version}/ltp-{ltp_version}'
        load_map(distance_metric, ltp_version)

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
                test_cases.append(f'{ltp_path}/{tc[16: -1]}')
            else:
                test_cases.append(tc[1: -1])

        try:
            prioritized_list = ARP(test_cases, k, distance_metric, candidate_stragety)

            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.info(f"distance_metric: {distance_metric}, commit: {git_sha}, apfd: {apfd_v}, ltp_version: {ltp_version}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}")
    return f"distance_metric: {distance_metric}, candidate_stragety: {candidate_stragety}, avg apfd: {np.average(apfd_res)}"