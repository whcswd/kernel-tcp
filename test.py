import json
from datetime import datetime

from liebes.CiObjects import *
from liebes.EHelper import EHelper
from liebes.GitHelper import GitHelper
from liebes.analysis import CIAnalysis
from liebes.sql_helper import SQLHelper
from liebes.tokenizer import *
import numpy as np
from liebes.llvm_process import LLVMHelper
import subprocess
import pymysql
import os
import threading
import multiprocessing as mp


linux_dir_path = r'/home/userpath/kernels/'
linux_origin_path = r'/home/userpath/linux-latest'
go_project_path = r'/home/userpath/projects/BitcodeGenerator4Kernel'
cg_dir_path = r'/home/userpath/kernels/kernel_cg'

def kernel_separation(git_sha: str):
    print(git_sha)
    cur_dir = linux_dir_path + git_sha
    if os.path.exists(cur_dir):
        return

    os.mkdir(cur_dir)

    cmd = f'cp -r {linux_origin_path} {cur_dir}/'
    subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')

    linux_dir = cur_dir + r'/linux-latest'
    cmd = f'cd {linux_dir} ; git checkout {git_sha}'
    subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')

def kernel_delete(git_sha: str):
    print(git_sha)
    cur_dir = linux_dir_path + git_sha
    if not os.path.exists(cur_dir):
        return
    
    cmd = f'rm -rf {cur_dir}'
    subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')

def kernel_make_config(git_sha: str):
    linux_path = linux_dir_path + git_sha + r'/linux-latest'
    cmd = f'cd {linux_path} ; make distclean'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    print(result)

    cmd = f'cd {linux_path} ; make defconfig'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    print(result)

    cmd = f'cp -f /home/userpath/kernels/.config {linux_path}/'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    print(result)

    # cmd = f'cd {linux_path} ; make LLVM=1 -j16'
    # result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    # print(result)

def generate_build_sh(git_sha: str):
    linux_path = linux_dir_path + git_sha + r'/linux-latest'
    cmd = f'cd {go_project_path} ; go run KernelBitcode.go -path={linux_path}'
    subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')

def kernel_bc_gen(git_sha: str):
    linux_path = linux_dir_path + git_sha + r'/linux-latest'
    cmd = f'cd {linux_path} ; chmod +x build.sh ; ./build.sh'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    print(result)

def kernel_cg_gen(git_sha: str):
    bc_path = linux_dir_path + git_sha + r'/linux-latest/built-in.bc'
    cmd = f'opt -load /home/userpath/llvm_kernel/build/helloPass/libHelloPass.so -helloCG < {bc_path}'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    # print(result)
    cg_path = cg_dir_path + f'/{git_sha}-cg.txt'
    cmd = f'cp -f ltp-output-cg.txt {cg_path}'
    subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')

def kernel_list_cg_gen(git_shas: str):
    for git_sha in git_shas:
        kernel_cg_gen(git_sha)

def kernel_list_bc_gen(git_shas: str):
    pool = mp.Pool()
    pool.map(kernel_bc_gen, git_shas)
    pool.close()
    pool.join()

def generate_list_build_sh(git_shas):
    pool = mp.Pool()
    pool.map(generate_build_sh, git_shas)
    pool.close()
    pool.join()

def kernel_list_make_config(git_shas):
    pool = mp.Pool()
    pool.map(kernel_make_config, git_shas)
    pool.close()
    pool.join()

def kernel_list_separation(git_shas):
    pool = mp.Pool()
    pool.map(kernel_separation, git_shas)
    pool.close()
    pool.join()

def kernel_list_delete(git_shas):
    pool = mp.Pool()
    pool.map(kernel_delete, git_shas)
    pool.close()
    pool.join()

if __name__ == '__main__':
    checkouts = []
    lines = []
    with open(r'logs/ci_info.txt', r'r') as f:
        for line in f:
            lines.append(line)
    f.close()

    history_infos = []
    for idx in range(len(lines)):
        cur_line = lines[idx]
        if r'checkout' in cur_line:
            commit_match = re.search(r'checkout: (.*?)\n', cur_line)
            git_sha = commit_match.group(1)
            checkouts.append(git_sha + '\n')
    
    
    for git_sha in checkouts:
        with open(r'checkout_with_failed_cases.txt', r'a') as f:
            f.write(git_sha)
        f.close()

    # filtered_results = []
    # with open(r'checkout_with_failed_cases.txt') as f:
    #     for git_sha in f:
    #         filtered_results.append(git_sha[: -1])
    # f.close()

    # kernel_list_make_config(filtered_results[10: 20])
    # print('continue run...')


    

    # data_size = len(results)
    # chunk_size = int(data_size / 40)

    # chunks = [results[i: i + chunk_size] for i in range(0, len(results), chunk_size)]
    # for chunk in chunks:
    #     thread = threading.Thread(target=kernel_list_separation, args=(chunk, ))
    #     thread.start()
    #     threads.append(thread)

    # for thread in threads:
    #     thread.join()


    