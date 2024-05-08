from liebes.Glibc_CG import CallGraph as G_CG
from liebes.CallGraph import CallGraph as CG
import os
from pathlib import Path
import pickle

with open(r'syscall_list', r'rb') as f:
    syscall_list = pickle.load(f)
f.close()

GLIBC_CG = G_CG(r'everything_graph.new')
coun = 0
for g in Path(r'/home/userpath/projects/selftests_cg').glob(r'*'):
    TEST_CG = CG()
    coun += 1
    cg_path = str(g.absolute())
    TEST_CG.load_from_source(cg_path)
    if 'main' not in TEST_CG.node_map.keys():
        print(cg_path)
    # ground_func_set = TEST_CG.get_ground_func(r'main')
    # used_syscall_set = set()
    # for gf in ground_func_set:
    #     used_syscall_set = used_syscall_set.union(GLIBC_CG.get_syscall(syscall_list, gf))

    # print(len(used_syscall_set))
# print(coun)