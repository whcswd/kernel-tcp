from datetime import datetime
from pathlib import Path
from liebes.sql_helper import SQLHelper
from liebes.CiObjects import DBCheckout, DBBuild, DBTestRun, DBTest, Checkout
from liebes.analysis import CIAnalysis
import openpyxl
from liebes.ci_logger import logger
from liebes.GitHelper import GitHelper
from liebes.Glibc_CG import CallGraph as G_CG
from liebes.CallGraph import CallGraph as CG
import pickle
import pathlib
import re

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

if __name__ == "__main__":
    GLIBC_CG = G_CG(r'everything_graph.new')
    KERNEL_CG = CG()
    KERNEL_CG.load_from_source(r'/home/userpath/kernels/kernel_cg/90450a06162e6c71ab813ea22a83196fe7cff4bc-cg.txt')
    sh_cg_dir = r'/home/userpath/projects/sh_cg'
    for d in pathlib.Path(sh_cg_dir).glob('*'):
        text = d.read_text()
        ground_func_list = re.findall(r'(.*?)\(', text)
        ground_func_set = set(ground_func_list)
        used_syscall_set = set()
        for f in ground_func_set:
            used_syscall_set = used_syscall_set.union(GLIBC_CG.get_syscall(syscall_list, f))
        related_method_set = set()
        for syscall in used_syscall_set:
            related_method_set = related_method_set.union(KERNEL_CG.get_all_call(syscall_table.get(syscall)))
        print(len(related_method_set))
        

