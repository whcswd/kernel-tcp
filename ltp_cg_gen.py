import json
from datetime import datetime

from liebes.CiObjects import *
from liebes.EHelper import EHelper
from liebes.GitHelper import GitHelper
from liebes.analysis import CIAnalysis
from liebes.ir_model import *
from liebes.sql_helper import SQLHelper
from liebes.tokenizer import *
import os
import subprocess


if __name__ == '__main__':
    # linux_path = '/home/userpath/linux-1'
    # sql = SQLHelper()
    # start_time = datetime.now()
    # checkouts = sql.session.query(DBCheckout).order_by(DBCheckout.git_commit_datetime.desc()).limit(500).all()
    # cia = CIAnalysis()
    # for ch in checkouts:
    #     cia.ci_objs.append(Checkout(ch))
    # cia.reorder()
    # cia.set_parallel_number(40)
    # cia.filter_job("COMBINE_SAME_CASE")
    # cia.filter_job("FILTER_SMALL_BRANCH", minimal_testcases=20)
    # cia.filter_job("COMBINE_SAME_CONFIG")
    # cia.filter_job("CHOOSE_ONE_BUILD")
    # cia.filter_job("FILTER_SMALL_BRANCH", minimal_testcases=20)
    # cia.filter_job("FILTER_FAIL_CASES_IN_LAST_VERSION")
    # cia.ci_objs = cia.ci_objs[1:]
    # cia.statistic_data()

    version = r'20230929'
    
    cg_dir = f'/home/userpath/projects/ltp_cg/ltp_{version}_cg/'

    # ltp_file_set = set()
    # for ci_obj in cia.ci_objs:
    #     for test in ci_obj.get_all_testcases():
    #         ltp_file_set.add(test.file_path)
    
    bc_dir = f'/home/userpath/projects/ltp_projects/ltp_{version}/ltp-{version}/testcases'

    def traverse_dep_file(dir_path):
        dep_bc_list = []
        for item in os.listdir(dir_path):
            item_path = os.path.join(dir_path, item)
            if os.path.isfile(item_path) and item.startswith('dep') and item.endswith('.bc'):
                dep_bc_list.append(str(item_path))
            elif os.path.isdir(item_path):
                dep_bc_list.extend(traverse_dep_file(item_path))
        
        return dep_bc_list
    
    dep_list = traverse_dep_file(bc_dir)
    for dep_bc in dep_list:
        file_name = dep_bc[dep_bc.rfind('/') + 1: ]
        file_name = file_name[4: ]
        cg_name = r'cg-' + file_name.replace(r'.bc', r'.txt')
        command = r'opt -load /home/userpath/llvm_kernel/build/helloPass/libHelloPass.so -helloCG < ' + dep_bc
        command = command + r' ; ' + f'mv -f ltp-output-cg.txt {cg_dir}{cg_name}'
        result = subprocess.run(command, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
        print(cg_name)


    # for ltp_file in ltp_file_set:
    #     ltp_file = str(ltp_file)
    #     ltp_file = ltp_file.replace(r'test_cases/ltp/', r'')
    #     if ltp_file.endswith(r'.c'):
    #         file_name = ltp_file[ltp_file.rfind(r'/') + 1: ]
    #         dep_file_name = 'dep_' + file_name.replace(r'.c', r'.bc')
    #         bc_path = bc_dir + ltp_file.replace(file_name, dep_file_name)
    #         if not os.path.exists(bc_path):
    #             continue

    #         cg_name = r'cg-' + file_name.replace(r'.c', r'.txt')
    #         command = r'opt -load /home/userpath/llvm_kernel/build/helloPass/libHelloPass.so -helloCG < ' + bc_path
    #         command = command + r' ; ' + f'mv ltp-output-cg.txt {cg_dir}{cg_name}'
            
    #         result = subprocess.run(command, shell=True, capture_output=True, text=True, encoding='ISO-8859-1')
    #         print(cg_name)

            

