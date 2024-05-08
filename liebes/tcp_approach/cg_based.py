from datetime import datetime
from pathlib import Path
from liebes.sql_helper import SQLHelper
from liebes.CiObjects import DBCheckout, DBBuild, DBTestRun, DBTest, Checkout, Test
from liebes import EHelper
from liebes.analysis import CIAnalysis
import openpyxl
from liebes.ci_logger import logger
from liebes.GitHelper import GitHelper
from liebes.Glibc_CG import CallGraph as G_CG
from liebes.CallGraph import CallGraph as CG
from liebes.EHelper import EHelper
import pickle
import pathlib
import os
import traceback
import numpy as np
import re
import multiprocessing as mp

ltp_cg_dir = r''
kselftests_cg_dir = r'/home/userpath/projects/selftests_cg'
kernel_cg_dir = r'/home/userpath/kernels/kernel_cg/'
sh_cg_dir = r'/home/userpath/projects/sh_cg'
ehelper = EHelper()


GLIBC_CG = G_CG(r'everything_graph.new')
KERNEL_CG = CG()
with open(r'syscall_list', r'rb') as f:
    syscall_list = pickle.load(f)
f.close()

with open(r'syscall_64.tbl') as f:
    syscall_64_tbl = f.read()
f.close()

syscall_table = {}

for line in syscall_64_tbl.split('\n'):
    if line.split('\t')[1] == 'x32':
        continue
    glibc_func = line.split('\t')[2]
    sys_call = line.split('\t')[-1]
    sys_call_func = '__do_' + sys_call
    syscall_table[glibc_func] = sys_call_func

def get_kernel_cg(git_sha: str):
    cg_file = kernel_cg_dir + git_sha + r'-cg.txt'
    # cg_file = r'output-cg.txt'
    KERNEL_CG.load_from_source(cg_file)

def get_ltp_cg(git_sha: str):
    ltp_version = ehelper.get_ltp_version(git_sha)
    global ltp_cg_dir
    ltp_cg_dir = r'/home/userpath/projects/ltp_cg/' + r'ltp_' + ltp_version + r'_cg'


def get_sh_used_syscall(file_path: str):
    test_name = file_path[file_path.rfind(r'/'): ]
    test_name = test_name.replace(r'.sh', r'.txt')
    if os.path.exists(sh_cg_dir + r'/' + test_name):
        with open(sh_cg_dir + r'/' + test_name, r'r') as f:
            text = f.read()
        f.close()
        ground_func_list = re.findall(r'(.*?)\(', text)
        ground_func_set = set(ground_func_list)
        used_syscall_set = set()
        for f in ground_func_set:
            used_syscall_set = used_syscall_set.union(GLIBC_CG.get_syscall(syscall_list, f))
        return used_syscall_set
    else:
        return set()

def get_userd_syscall(file_path: str):
    if r'.sh' in file_path:
        return get_sh_used_syscall(file_path)
    test_name = file_path[file_path.rfind(r'/') + 1: -2]
    TEST_CG = CG()
    test_path = os.path.join(ltp_cg_dir, r'cg-' + test_name + r'.txt')
    if not os.path.exists(test_path):
        test_path = os.path.join(kselftests_cg_dir, r'cg-' + test_name + r'.txt')
        if not os.path.exists(test_path):
            return set()
    TEST_CG.load_from_source(test_path)
    top_func_set = set()
    if r'selftest' in file_path:
        top_func_set.add(r'main')
    else:
        top_func_set = TEST_CG.get_top_func(test_name)
    # print(test_path)
    ground_func_set = set()
    for top_func in top_func_set:
        ground_func_set = ground_func_set.union(TEST_CG.get_ground_func(top_func))
    # ground_func_set = TEST_CG.get_ground_func()
    used_syscall_set = set()
    for func in ground_func_set:
        used_syscall_set = used_syscall_set.union(GLIBC_CG.get_syscall(syscall_list, func))
    
    return used_syscall_set


def get_related_methods(file_path: str):
    used_syscall_set = get_userd_syscall(file_path)
    related_method_set = set()
    for syscall in used_syscall_set:
        related_method_set = related_method_set.union(KERNEL_CG.get_all_call(syscall_table.get(syscall)))

    TA_tmp[file_path] = related_method_set
    return related_method_set

TA_tmp = {}

# def total_stragety(test_cases: list[Test]):
#     test_TA_map = {}
#     for idx in range(len(test_cases)):
#         test_case = test_cases[idx]
#         if not test_case.file_path.endswith(r'.c'):
#             test_TA_map[idx] = 0
#             continue
#         if TA_tmp.get(test_case.file_path) is None:
#             related_methods_set = get_related_methods(test_case.file_path)
#             test_TA_map[idx] = len(related_methods_set)
#         else:
#             test_TA_map[idx] = len(TA_tmp[test_case.file_path])
    
#     sorted_map = dict(sorted(test_TA_map.items(), key=lambda x: x[1], reverse=True))
#     sorted_list = list(sorted_map.keys())
#     return sorted_list

def total_stragety(test_cases: list[str]):
    test_TA_map = {}
    for idx in range(len(test_cases)):
        test_case = test_cases[idx]
        # if not test_case.endswith(r'.c'):
        #     test_name = test_case[test_case.rfind(r'/'): ]
        #     test_name = test_name.replace(r'.sh', r'.txt')
        #     if os.path.exists(sh_cg_dir + r'/' + test_name):
        #         test_TA_map[idx] = 7500
        #     else:
        #         test_TA_map[idx] = 0
        #     continue
        related_methods_set = get_related_methods(test_case)
        test_TA_map[idx] = len(related_methods_set)
    
    sorted_map = dict(sorted(test_TA_map.items(), key=lambda x: x[1], reverse=True))
    sorted_list = list(sorted_map.keys())
    return sorted_list

# def additional_stragety(test_cases: list[Test]):
#     test_idx_map = {}
#     for idx in range(len(test_cases)):
#         test_case = test_cases[idx]
#         test_idx_map[test_case.file_path] = idx
#     slide_set = set()
#     size = len(test_cases)
#     sorted_list = []

#     for test_case in test_cases:
#         related_methods_set = TA_tmp.get(test_case.file_path)
#         if related_methods_set is None:
#             related_methods_set = get_related_methods(test_case)
#             TA_tmp[test_case.file_path] = related_methods_set

#     while size > 0:
#         max_TA = -1
#         max_TA_test = None
#         max_related_method_set = set()
#         for test_case in test_cases:
#             related_methods_set = TA_tmp.get(test_case.file_path)
#             additional_method_set = related_methods_set - slide_set
#             if len(additional_method_set) > max_TA:
#                 max_TA = len(related_methods_set)
#                 max_TA_test = test_case
#                 max_related_method_set = related_methods_set
        
#         if max_TA == 0:
#             slide_set = set()
#         else:
#             slide_set = slide_set.union(max_related_method_set)
#         sorted_list.append(test_idx_map[max_TA_test.file_path])
#         test_cases.remove(max_TA_test)
#         size -= 1
        
#     return sorted_list

def additional_stragety(test_cases: list[str]):
    test_idx_map = {}
    for idx in range(len(test_cases)):
        test_case = test_cases[idx]
        test_idx_map[test_case] = idx
    slide_set = set()
    size = len(test_cases)
    sorted_list = []

    for test_case in test_cases:
        related_methods_set = get_related_methods(test_case)
        TA_tmp[test_case] = related_methods_set

    while size > 0:
        max_TA = -1
        max_TA_test = None
        max_related_method_set = set()
        for test_case in test_cases:
            related_methods_set = TA_tmp.get(test_case)
            additional_method_set = related_methods_set - slide_set
            if len(additional_method_set) > max_TA:
                max_TA = len(additional_method_set)
                max_TA_test = test_case
                max_related_method_set = related_methods_set
        
        if max_TA == 0:
            slide_set = set()
        else:
            slide_set = slide_set.union(max_related_method_set)
        sorted_list.append(test_idx_map[max_TA_test])
        test_cases.remove(max_TA_test)
        size -= 1
        
    return sorted_list


def do_exp(cia: CIAnalysis, stragety: str):
    apfd_res = []
    for ci_index in range(len(cia.ci_objs)):
        ci_obj = cia.ci_objs[ci_index]
        test_cases = ci_obj.get_all_testcases()
        faults_arr = []
        for i in range(len(test_cases)):
            if test_cases[i].is_failed():
                # if r'selftests' in test_cases[i].file_path:
                    # continue
                faults_arr.append(i)
        if len(faults_arr) == 0:
            continue

        get_kernel_cg(ci_obj.instance.git_sha)
        get_ltp_cg(ci_obj.instance.git_sha)
        try:
            if stragety == 'total':
                prioritized_list = total_stragety(test_cases)
            elif stragety == 'additional':
                prioritized_list = additional_stragety(test_cases)
            else:
                return 'Error Stragety!!!'
            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.debug(f"stragety: {stragety}, commit: {ci_obj.instance.git_sha}, apfd: {apfd_v}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}")
    return f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}"

def do_exp2(log_path: str, stragety: str):
    apfd_res = []
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if 'hanming_distance' in cur_line and r'commit' in cur_line:
            commit_match = re.search(r'commit: (.*?),', cur_line)
            git_sha = commit_match.group(1)
            next_line = lines[idx + 1]
            prioritized_match = re.search(r'prioritized_list: \[(.*?)\]', next_line)
            prioritized_list = prioritized_match.group(1).split(', ')
            next_2_line = lines[idx + 2]
            fault_match = re.search(r'faults_arr: \[(.*?)\]', next_2_line)
            fault_list = fault_match.group(1).split(', ')
            history_infos.append((git_sha, prioritized_list, fault_list))
    
    for hi in history_infos:
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
            test_cases.append(tc[1: -1])
        
        get_kernel_cg(git_sha)
        get_ltp_cg(git_sha)
        try:
            if stragety == 'total':
                prioritized_list = total_stragety(test_cases)
            elif stragety == 'additional':
                prioritized_list = additional_stragety(test_cases)
            else:
                return 'Error Stragety!!!'
            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.debug(f"stragety: {stragety}, commit: {git_sha}, apfd: {apfd_v}")
            # logger.info(f'{prioritized_list}')
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}")
    return f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}"

def do_exp3(log_path: str, stragety: str, gap_version: int):
    apfd_res = []
    lines = []
    with open(log_path, r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = ehelper.get_checkout_info()
    
    for hi_idx in range(len(history_infos)):
        hi = history_infos[hi_idx]
        refer_idx = hi_idx + gap_version
        if refer_idx >= len(history_infos):
            break
        git_sha = history_infos[refer_idx][0]
        # if git_sha != r'3a8a670eeeaa40d87bd38a587438952741980c18':
        #     continue
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
            test_cases.append(tc[1: -1])
        
        get_kernel_cg(git_sha)
        get_ltp_cg(git_sha)
        try:
            if stragety == 'total':
                prioritized_list = total_stragety(test_cases)
            elif stragety == 'additional':
                prioritized_list = additional_stragety(test_cases)
            else:
                return 'Error Stragety!!!'
            apfd_v = ehelper.APFD(faults_arr, prioritized_list)
            logger.debug(f"stragety: {stragety}, commit: {git_sha}, apfd: {apfd_v}, gap version: {gap_version}")
            apfd_res.append(apfd_v)
        except:
            traceback.print_exc()
    logger.info(f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}, gap version: {gap_version}")
    return f"stragety: {stragety}, avg apfd: {np.average(apfd_res)}, gap version: {gap_version}"

def do_exp4_unit(params):
    stragety = params[0]
    hi = params[1]
    apfd_res = []
    
    git_sha = hi[0]
    # if git_sha != r'3a8a670eeeaa40d87bd38a587438952741980c18':
    #     continue
    test_cases = []
    faults_arr = []

    for fault in hi[2]:
        # if r'selftests' in fault:
        #     continue
        for idx in range(len(hi[1])):
            if fault == hi[1][idx]:
                faults_arr.append(idx)

    for tc in hi[1]:
        test_cases.append(tc[1: -1])
    
    get_kernel_cg(git_sha)
    get_ltp_cg(git_sha)
    try:
        if stragety == 'total':
            prioritized_list = total_stragety(test_cases)
        elif stragety == 'additional':
            prioritized_list = additional_stragety(test_cases)
        else:
            return 'Error Stragety!!!'
        apfd_v = ehelper.APFD(faults_arr, prioritized_list)
        logger.debug(f"stragety: {stragety}, commit: {git_sha}, apfd: {apfd_v}")
    except:
        traceback.print_exc()

def do_exp4(stragety: str):
    histroy_infos = ehelper.get_checkout_info()
    params_list = []
    for hi in histroy_infos:
        params_list.append((stragety, hi))
    pool = mp.Pool()
    pool.map(do_exp4_unit, params_list)
    pool.close()
    pool.join()
    